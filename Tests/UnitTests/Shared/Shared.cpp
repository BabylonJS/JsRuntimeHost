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
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
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
    // Regression test for a race condition in the AppRuntime destructor.
    // The old code cancelled from the main thread, which could race with
    // blocking_tick's condition_variable::wait, causing a deadlock on join().
    //
    // This test uses an arcana testing hook to guarantee the worker thread is
    // inside condition_variable::wait before destroying the runtime, making
    // the race deterministic.
    for (int i = 0; i < 10; i++)
    {
        auto runtime = std::make_unique<Babylon::AppRuntime>();

        // Use the before-wait hook to know exactly when the worker thread
        // is about to enter condition_variable::wait.
        std::promise<void> workerWaiting;
        bool signaled = false;
        runtime->SetBeforeWaitCallback([&]() {
            if (!signaled)
            {
                signaled = true;
                workerWaiting.set_value();
            }
        });

        // Dispatch a no-op to ensure the runtime is initialized and the
        // worker thread enters the blocking_tick loop.
        std::promise<void> initialized;
        runtime->Dispatch([&initialized](Napi::Env) {
            initialized.set_value();
        });
        initialized.get_future().wait();

        // Wait until the worker thread is inside blocking_tick's wait.
        workerWaiting.get_future().wait();

        // Destroy the runtime on a separate thread with a timeout.
        // If the destructor deadlocks, the timeout will fire.
        auto destroyFuture = std::async(std::launch::async, [&runtime]() {
            runtime.reset();
        });

        auto status = destroyFuture.wait_for(std::chrono::seconds(5));
        ASSERT_NE(status, std::future_status::timeout)
            << "Deadlock detected: AppRuntime destructor did not complete within 5 seconds (iteration " << i << ")";
    }
}

int RunTests()
{
    testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}
