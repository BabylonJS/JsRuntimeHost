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
#include <Babylon/Polyfills/Fetch.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/File.h>
#include <Babylon/Polyfills/TextDecoder.h>
#include <Babylon/Polyfills/TextEncoder.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdint>
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
        Babylon::Polyfills::Console::Initialize(env, [env](const char* message, Babylon::Polyfills::Console::LogLevel logLevel) {
            std::cout << "[" << EnumToString(logLevel) << "] " << message;
            if (logLevel == Babylon::Polyfills::Console::LogLevel::Error)
            {
                std::string stack = Babylon::Polyfills::Console::CaptureCurrentJsStack(env);
                if (!stack.empty())
                {
                    std::cout << std::endl << stack;
                }
            }
            std::cout << std::endl;
            std::cout.flush();
        });

        Babylon::Polyfills::AbortController::Initialize(env);
        Babylon::Polyfills::Performance::Initialize(env);
        Babylon::Polyfills::Scheduling::Initialize(env);
        Babylon::Polyfills::URL::Initialize(env);
        Babylon::Polyfills::WebSocket::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        Babylon::Polyfills::Fetch::Initialize(env);
        Babylon::Polyfills::Blob::Initialize(env);
        Babylon::Polyfills::File::Initialize(env);
        Babylon::Polyfills::TextDecoder::Initialize(env);
        Babylon::Polyfills::TextEncoder::Initialize(env);

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

TEST(Console, CaptureCurrentJsStack)
{
    // Regression: Console::CaptureCurrentJsStack must return a non-empty stack when called from
    // within a callback fired by `console.error`, and when called from `console.log` (any frame
    // produced by JS execution).
    Babylon::AppRuntime runtime{};

    std::promise<std::string> errorStackPromise;
    std::promise<std::string> logStackPromise;

    runtime.Dispatch([&errorStackPromise, &logStackPromise](Napi::Env env) mutable {
        Babylon::Polyfills::Console::Initialize(env, [env, &errorStackPromise, &logStackPromise](const char* /*message*/, Babylon::Polyfills::Console::LogLevel logLevel) {
            std::string stack = Babylon::Polyfills::Console::CaptureCurrentJsStack(env);
            if (logLevel == Babylon::Polyfills::Console::LogLevel::Error)
            {
                errorStackPromise.set_value(std::move(stack));
            }
            else if (logLevel == Babylon::Polyfills::Console::LogLevel::Log)
            {
                logStackPromise.set_value(std::move(stack));
            }
        });
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval("console.log('log message');", "");
    loader.Eval("function inner() { console.error('error message'); } inner();", "");

    auto errorFuture = errorStackPromise.get_future();
    auto logFuture = logStackPromise.get_future();
    constexpr auto timeout = std::chrono::seconds(30);
    ASSERT_EQ(errorFuture.wait_for(timeout), std::future_status::ready)
        << "console.error callback did not fire within timeout";
    ASSERT_EQ(logFuture.wait_for(timeout), std::future_status::ready)
        << "console.log callback did not fire within timeout";

    std::string errorStack = errorFuture.get();
    std::string logStack = logFuture.get();

    EXPECT_FALSE(errorStack.empty()) << "console.error path must capture a non-empty JS stack";
    EXPECT_FALSE(logStack.empty()) << "console.log path must capture a non-empty JS stack";
}

TEST(AppRuntime, DestroyDoesNotDeadlock)
{
    // Regression test verifying AppRuntime destruction completes
    // promptly across the full range of worker-thread states.
    //
    // History: issue #147 fixed a specific race in the original
    // blocking_drain-based dispatcher loop where the destructor's
    // push() could deadlock if the worker held the queue mutex
    // inside wait(). The dispatcher loop has since been reworked
    // (non-blocking tick + brief sleep + post-tick hook) so the
    // engine-side task queues (notably V8's foreground task runner,
    // for async WebAssembly compile continuations) can be drained
    // between ticks. The blocking_drain race window no longer
    // exists in that design, but ~AppRuntime is still required to
    // unblock the worker thread promptly under all conditions.
    //
    // We exercise three worker states to cover the dispatch loop:
    //   1. Idle           -- worker is in tick() / sleep()
    //   2. In a callback  -- worker is running a Dispatch handler
    //   3. Busy queue     -- many pending dispatches when ~AppRuntime
    //                        fires; the destructor drops them.
    //
    // Each lifecycle is run on a separate thread so the gtest thread
    // can detect a deadlock via wait_for() without hanging the
    // process.

    constexpr auto destructionTimeout = std::chrono::seconds(5);

    auto runLifecycleOnThread = [destructionTimeout](const char* label, auto setup)
    {
        std::promise<void> done;
        std::thread t([&]()
        {
            auto runtime = std::make_unique<Babylon::AppRuntime>();

            // Ensure the runtime is fully initialized (CreateForJavaScript
            // queued by the AppRuntime constructor has run).
            std::promise<void> ready;
            runtime->Dispatch([&ready](Napi::Env) { ready.set_value(); });
            ready.get_future().wait();

            setup(*runtime);

            // Destroy -- must complete promptly.
            runtime.reset();
            done.set_value();
        });

        auto status = done.get_future().wait_for(destructionTimeout);
        if (status == std::future_status::timeout)
        {
            t.detach();
            FAIL() << "Deadlock detected in scenario '" << label
                   << "': AppRuntime destructor did not complete within "
                   << destructionTimeout.count() << " seconds";
        }
        else
        {
            t.join();
        }
    };

    // Scenario 1: idle worker. The constructor's initial CreateForJavaScript
    // dispatch has already drained by the time we destroy; the worker is
    // sitting in the tick + sleep loop with no pending work.
    runLifecycleOnThread("idle worker", [](Babylon::AppRuntime&) {
        // Nothing -- destroy immediately after construction.
    });

    // Scenario 2: destroy while a callback is in flight. We dispatch a
    // handler that signals it has started running, then sleeps. The
    // destructor is invoked while that handler is still on the worker
    // thread, exercising the codepath that cancels mid-callback.
    runLifecycleOnThread("callback in flight", [](Babylon::AppRuntime& runtime) {
        std::promise<void> handlerStarted;
        runtime.Dispatch([&handlerStarted](Napi::Env) {
            handlerStarted.set_value();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });
        handlerStarted.get_future().wait();
    });

    // Scenario 3: backlog at destruction. Queue many no-op dispatches
    // without waiting for any to complete, then destroy. The destructor
    // must drop the unprocessed work without waiting on it.
    runLifecycleOnThread("backlog at destruction", [](Babylon::AppRuntime& runtime) {
        for (int i = 0; i < 50; ++i)
        {
            runtime.Dispatch([](Napi::Env) {});
        }
    });
}

// The V8JSI Node-API shim does not implement napi_create_dataview /
// napi_get_dataview_info (its DataView::New throws "TODO"), so this native test
// only builds on the Chakra, V8, and JavaScriptCore backends. The size_t-width
// guard is required because the overflow scenario below needs a 64-bit size_t.
#if (SIZE_MAX > 0xFFFFFFFFu) && !defined(JSRUNTIMEHOST_NAPI_ENGINE_JSI)
TEST(NodeApi, CreateDataViewRejectsOverflowingRange)
{
    // Regression: napi_create_dataview must reject a (byte_offset, byte_length)
    // pair whose sum overflows size_t. The pre-fix code performed an unchecked
    // `byte_offset + byte_length > bufferLength` comparison; with the inputs
    // below the 64-bit sum wraps to 8 and slips past it. It then truncated the
    // values to 32-bit (offset -> 0, length -> 8) and created a valid 8-byte
    // DataView, but stored the ORIGINAL 64-bit offset/length in DataViewInfo,
    // which napi_get_dataview_info hands back alongside the small real buffer --
    // an out-of-bounds access primitive. This path is not reachable from JS
    // `new DataView`, so it is covered natively here. The scenario requires a
    // 64-bit size_t (where the 32-bit truncation diverged from the stored value),
    // hence the size_t-width guard.
    Babylon::AppRuntime runtime{};

    std::promise<bool> overflowSafe;
    std::promise<bool> validAccepted;

    runtime.Dispatch([&overflowSafe, &validAccepted](Napi::Env env) {
        napi_env nenv{env};

        Napi::ArrayBuffer arrayBuffer{Napi::ArrayBuffer::New(env, 16)};
        napi_value arrayBufferValue{arrayBuffer};

        // Low 32 bits are individually valid for the 16-byte buffer (offset 0,
        // length 8), but the full 64-bit values are enormous and their sum wraps
        // around size_t to 8.
        const size_t hugeOffset{0xFFFFFFFF00000000ull};
        const size_t hugeLength{0x0000000100000008ull};

        napi_value result{nullptr};
        napi_status status{napi_create_dataview(nenv, hugeLength, arrayBufferValue, hugeOffset, &result)};

        bool safe;
        if (status != napi_ok || result == nullptr)
        {
            // Fixed path: the out-of-range request is rejected outright.
            safe = true;
        }
        else
        {
            // If creation unexpectedly succeeds, the reported extents must still
            // lie within the 16-byte backing buffer (i.e. not the raw 64-bit
            // inputs). The pre-fix code reported the huge stored values here.
            size_t reportedLength{0};
            size_t reportedOffset{0};
            void* data{nullptr};
            napi_get_dataview_info(nenv, result, &reportedLength, &data, nullptr, &reportedOffset);
            safe = reportedOffset <= 16 && reportedLength <= 16 && reportedOffset + reportedLength <= 16;
        }

        // Clear any pending range error so it doesn't surface as an unhandled error.
        napi_value pendingException{nullptr};
        napi_get_and_clear_last_exception(nenv, &pendingException);
        overflowSafe.set_value(safe);

        // A legitimate offset/length pair must still succeed.
        napi_value validResult{nullptr};
        napi_status validStatus{napi_create_dataview(nenv, 8, arrayBufferValue, 4, &validResult)};
        validAccepted.set_value(validStatus == napi_ok && validResult != nullptr);
    });

    EXPECT_TRUE(overflowSafe.get_future().get());
    EXPECT_TRUE(validAccepted.get_future().get());
}
#endif

int RunTests()
{
    testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}
