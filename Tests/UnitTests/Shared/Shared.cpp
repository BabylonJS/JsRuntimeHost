#include "Shared.h"
#include <Babylon/AppRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/AbortController.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Performance.h>
#include <Babylon/Polyfills/Scheduling.h>
#include <Babylon/Polyfills/URL.h>
#include <Babylon/Polyfills/WebSocket.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/TextDecoder.h>
#include <gtest/gtest.h>
#include <arcana/threading/blocking_concurrent_queue.h>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

namespace
{
    const char* EnumToString(Babylon::Polyfills::Console::LogLevel logLevel)
    {
        switch (logLevel)
        {
            case Babylon::Polyfills::Console::LogLevel::Log:
                return "log";
            case Babylon::Polyfills::Console::LogLevel::Warn:
                return "warn";
            case Babylon::Polyfills::Console::LogLevel::Error:
                return "error";
        }

        return "unknown";
    }
}

TEST(JavaScript, All)
{
    // Change this to true to wait for the JavaScript debugger to attach (only applies to V8)
    constexpr const bool waitForDebugger = false;

    std::promise<int32_t> exitCodePromise;

    Babylon::AppRuntime::Options options{};

    options.UnhandledExceptionHandler = [&exitCodePromise](const Napi::Error& error) {
        std::cerr << "[Uncaught Error] " << Napi::GetErrorString(error) << std::endl;
        std::cerr.flush();

        exitCodePromise.set_value(-1);
    };

    if (waitForDebugger)
    {
        std::cout << "Waiting for debugger..." << std::endl;
        options.WaitForDebugger = true;
    }

    Babylon::AppRuntime runtime{options};

    runtime.Dispatch([&exitCodePromise](Napi::Env env) mutable {
        Babylon::Polyfills::Console::Initialize(env, [](const char* message, Babylon::Polyfills::Console::LogLevel logLevel) {
            std::cout << "[" << EnumToString(logLevel) << "] " << message << std::endl;
            std::cout.flush();
        });

        Babylon::Polyfills::AbortController::Initialize(env);
        Babylon::Polyfills::Performance::Initialize(env);
        Babylon::Polyfills::Scheduling::Initialize(env);
        Babylon::Polyfills::URL::Initialize(env);
        Babylon::Polyfills::WebSocket::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        Babylon::Polyfills::Blob::Initialize(env);
        Babylon::Polyfills::TextDecoder::Initialize(env);

        auto setExitCodeCallback = Napi::Function::New(
            env, [&exitCodePromise](const Napi::CallbackInfo& info) {
                Napi::Env env = info.Env();
                exitCodePromise.set_value(info[0].As<Napi::Number>().Int32Value());
            },
            "setExitCode");
        env.Global().Set("setExitCode", setExitCodeCallback);

        env.Global().Set("hostPlatform", Napi::Value::From(env, JSRUNTIMEHOST_PLATFORM));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval("location = { href: '' };", ""); // Required for Mocha.js as we do not have a location
    loader.LoadScript("app:///Scripts/tests.js");

    auto exitCode{exitCodePromise.get_future().get()};

    EXPECT_EQ(exitCode, 0);
}

TEST(Console, Log)
{
    Babylon::AppRuntime runtime{};

    runtime.Dispatch([](Napi::Env env) mutable {
        Babylon::Polyfills::Console::Initialize(env, [](const char* message, Babylon::Polyfills::Console::LogLevel logLevel) {
            const char* test = "foo bar";
            if (strcmp(message, test) != 0)
            {
                std::cout << "Expected: " << test << std::endl;
                std::cout << "Received: " << message << std::endl;
                std::cout.flush();
                ADD_FAILURE();
            }
        });
    });

    std::promise<void> done;

    Babylon::ScriptLoader loader{runtime};
    loader.Eval("console.log('foo', 'bar')", "");
    loader.Dispatch([&done](auto) {
        done.set_value();
    });

    done.get_future().get();
}

TEST(AppRuntime, DestroyDoesNotDeadlock)
{
    // Regression test verifying AppRuntime destruction doesn't deadlock.
    // Uses a global arcana hook to sleep while holding the queue mutex
    // before wait(), ensuring the worker is in the vulnerable window
    // when the destructor fires. See #147 for details on the bug and fix.
    //
    // The entire test runs on a separate thread so the gtest thread can
    // detect a deadlock via timeout without hanging the process.
    //
    // Test flow:
    //
    //   Test Thread                    Worker Thread
    //   -----------                    -------------
    //   1. Create AppRuntime           Worker starts, enters blocking_tick
    //      Wait for init to complete
    //   2. Install hook
    //      Dispatch(no-op)             Worker wakes, runs no-op,
    //                                  returns to blocking_tick
    //                                  Hook fires:
    //                                    signal workerInHook
    //                                    sleep 200ms (holding mutex!)
    //   3. workerInHook.wait()
    //      Worker is sleeping in hook
    //   4. ~AppRuntime():
    //          cancel()
    //          Append(no-op):
    //            push() blocks ------> (worker holds mutex)
    //                                  200ms sleep ends
    //                                  wait(lock) releases mutex
    //            push() acquires mutex
    //            pushes, notifies ---> wakes up!
    //            join() waits          drains no-op, cancelled -> exit
    //            join() returns <----- thread exits
    //   5. destroy completes -> PASS

    // Shared state on the heap so a detached thread (on timeout)
    // doesn't access destroyed stack variables.
    struct State
    {
        bool hookSignaled{false};
        std::promise<void> workerInHook;
        std::promise<void> testDone;
    };
    auto state = std::make_shared<State>();

    // Run the full lifecycle on a separate thread so the gtest thread
    // can detect a deadlock via timeout.
    std::thread testThread([state]() {
        auto runtime = std::make_unique<Babylon::AppRuntime>();

        // Wait for the runtime to fully initialize. The constructor dispatches
        // CreateForJavaScript which must complete before we install the hook
        // so the worker is idle and ready to enter the hook on the next wait.
        std::promise<void> ready;
        runtime->Dispatch([&ready](Napi::Env) {
            ready.set_value();
        });
        ready.get_future().wait();

        // Install the hook and dispatch a no-op to wake the worker,
        // ensuring it cycles through the hook on its way back to idle.
        arcana::test_hooks::blocking_concurrent_queue::set_before_wait_callback([state]() {
            if (!state->hookSignaled)
            {
                state->hookSignaled = true;
                state->workerInHook.set_value();
            }
            // This sleep is not truly deterministic — it creates a timing window
            // during which the destructor's push() will contend for the mutex.
            // The sleep holds the mutex, so push() blocks until it ends and the
            // worker enters wait(). This is sufficient for testing but relies on
            // the destructor firing within this window.
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        });
        runtime->Dispatch([](Napi::Env) {});

        // Wait for the worker to be in the hook (holding mutex, sleeping)
        state->workerInHook.get_future().wait();

        // Destroy — if the fix works, the destructor completes.
        // If broken, it deadlocks and the timeout detects it.
        runtime.reset();
        state->testDone.set_value();
    });

    auto status = state->testDone.get_future().wait_for(std::chrono::seconds(5));

    arcana::test_hooks::blocking_concurrent_queue::set_before_wait_callback([]() {});

    if (status == std::future_status::timeout)
    {
        // The process is in an unrecoverable state (deadlocked thread).
        // Terminate immediately — continuing would cause undefined behavior
        // from the detached thread accessing process-wide state.
        testThread.detach();
        std::cerr << "FAIL: Deadlock detected: AppRuntime destructor did not complete within 5 seconds" << std::endl;
        std::quick_exit(1);
    }

    testThread.join();
}

int RunTests()
{
    testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}
