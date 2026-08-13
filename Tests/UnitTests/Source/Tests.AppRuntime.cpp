#include <Babylon/AppRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <gtest/gtest.h>
#include <arcana/threading/blocking_concurrent_queue.h>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

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

    bool hookSignaled{false};
    std::promise<void> workerInHook;
    std::promise<void> testDone;

    // Run the full lifecycle on a separate thread so the gtest thread
    // can detect a deadlock via timeout.
    std::thread testThread([&]() {
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
        arcana::test_hooks::blocking_concurrent_queue::set_before_wait_callback([&]() {
            if (hookSignaled)
            {
                return;
            }
            hookSignaled = true;
            workerInHook.set_value();
            // This sleep is not truly deterministic. Its purpose is to hold the
            // mutex long enough for runtime.reset() (called by the test thread
            // after workerInHook signals) to reach push() while the mutex is
            // still held. When the sleep ends, the worker enters wait() which
            // releases the mutex, allowing push() to acquire it and deliver the
            // wake-up notification. If runtime.reset() hasn't reached push()
            // by the time the sleep ends, the test still passes but doesn't
            // exercise the intended contention window.
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        });
        runtime->Dispatch([](Napi::Env) {});

        // Wait for the worker to be in the hook (holding mutex, sleeping)
        workerInHook.get_future().wait();

        // Destroy — if the fix works, the destructor completes.
        // If broken, it deadlocks and the timeout detects it.
        runtime.reset();
        testDone.set_value();
    });

    auto status = testDone.get_future().wait_for(std::chrono::seconds(5));

    arcana::test_hooks::blocking_concurrent_queue::set_before_wait_callback([]() {});

    if (status == std::future_status::timeout)
    {
        testThread.detach();
        FAIL() << "Deadlock detected: AppRuntime destructor did not complete within 5 seconds";
    }

    testThread.join();
}

TEST(AppRuntime, UnhandledPromiseRejectionReachesHandler)
{
    // Unhandled promise rejection tracking is implemented on the engines that expose a host
    // promise-rejection hook: V8 (Isolate::SetPromiseRejectCallback) and Apple JavaScriptCore
    // (JSGlobalContextSetUnhandledRejectionCallback, an SPI absent from WebKitGTK/Linux JSC). The OS
    // EdgeMode Chakra runtime and the V8JSI (JSI) shim expose no such hook, so the body is compiled
    // out there (and on non-Apple JSC) and the test is skipped.
#if !(defined(JSRUNTIMEHOST_NAPI_ENGINE_V8) || (defined(JSRUNTIMEHOST_NAPI_ENGINE_JavaScriptCore) && defined(__APPLE__)))
    GTEST_SKIP() << "unhandled promise rejection tracking requires the V8 or Apple JavaScriptCore backend";
#else
    // A fire-and-forget rejected promise (no handler ever attached) must reach the embedder's
    // UnhandledExceptionHandler.
    Babylon::AppRuntime::Options options{};

    std::promise<std::string> rejectionMessage;
    auto future = rejectionMessage.get_future();

    // UnhandledExceptionHandler is the runtime's handler for every unhandled error, not just
    // rejections, so it can fire more than once; a second set_value would throw std::future_error
    // out of the handler.
    std::atomic<bool> reported{false};
    options.UnhandledExceptionHandler = [&rejectionMessage, &reported](const Napi::Error& error) {
        if (!reported.exchange(true))
        {
            rejectionMessage.set_value(error.Message());
        }
    };

    Babylon::AppRuntime runtime{options};

    Babylon::ScriptLoader loader{runtime};
    loader.Eval("Promise.reject(new Error('boom from fire-and-forget'));", "");

    ASSERT_EQ(future.wait_for(std::chrono::seconds(30)), std::future_status::ready)
        << "unhandled rejection did not reach the host handler";
    EXPECT_NE(future.get().find("boom from fire-and-forget"), std::string::npos);
#endif
}

TEST(AppRuntime, SynchronouslyHandledRejectionDoesNotReachHandler)
{
    // Only engines with a host promise-rejection hook implement this tracking (see the note above).
#if !(defined(JSRUNTIMEHOST_NAPI_ENGINE_V8) || (defined(JSRUNTIMEHOST_NAPI_ENGINE_JavaScriptCore) && defined(__APPLE__)))
    GTEST_SKIP() << "unhandled promise rejection tracking requires the V8 or Apple JavaScriptCore backend";
#else
    // A rejection that is handled synchronously in the same turn must NOT reach the handler --
    // reporting is deferred to the end of the turn, by which point the .catch has been attached.
    Babylon::AppRuntime::Options options{};

    std::atomic<bool> handlerFired{false};
    options.UnhandledExceptionHandler = [&handlerFired](const Napi::Error&) {
        handlerFired = true;
    };

    Babylon::AppRuntime runtime{options};

    Babylon::ScriptLoader loader{runtime};
    loader.Eval("const p = Promise.reject(new Error('handled')); p.catch(() => {});", "");

    // Round-trip a dispatch so any deferred rejection-flush task has run before we check.
    std::promise<void> drained;
    loader.Dispatch([&drained](Napi::Env) { drained.set_value(); });
    drained.get_future().wait();

    EXPECT_FALSE(handlerFired.load()) << "a synchronously-handled rejection must not reach the host handler";
#endif
}

TEST(AppRuntime, NonErrorRejectionReasonsStillCarryAMessage)
{
    // Only engines with a host promise-rejection hook implement this tracking (see the note above).
#if !(defined(JSRUNTIMEHOST_NAPI_ENGINE_V8) || (defined(JSRUNTIMEHOST_NAPI_ENGINE_JavaScriptCore) && defined(__APPLE__)))
    GTEST_SKIP() << "unhandled promise rejection tracking requires the V8 or Apple JavaScriptCore backend";
#else
    // A promise can be rejected with any value, but the host handler takes a Napi::Error. Whatever
    // the reason is, the error it arrives as has to carry a usable message -- reporting a rejection
    // with an empty message is barely better than not reporting it.
    const auto reportedMessageFor = [](const char* script) {
        Babylon::AppRuntime::Options options{};

        std::promise<std::string> rejectionMessage;
        auto future = rejectionMessage.get_future();

        std::atomic<bool> reported{false};
        options.UnhandledExceptionHandler = [&rejectionMessage, &reported](const Napi::Error& error) {
            if (!reported.exchange(true))
            {
                rejectionMessage.set_value(error.Message());
            }
        };

        Babylon::AppRuntime runtime{options};

        Babylon::ScriptLoader loader{runtime};
        loader.Eval(script, "");

        if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready)
        {
            return std::string{"<no rejection reported>"};
        }

        return future.get();
    };

    // A string reason is stringified.
    EXPECT_NE(reportedMessageFor("Promise.reject('a plain string reason');").find("a plain string reason"), std::string::npos);

    // An object carrying a message is error-like enough to pass through with it, even though it is
    // not a native Error.
    EXPECT_NE(reportedMessageFor("Promise.reject({ message: 'error-like object' });").find("error-like object"), std::string::npos);

    // A plain object has no message to pass through, so it must be stringified rather than yielding
    // an error with an empty message.
    EXPECT_FALSE(reportedMessageFor("Promise.reject({ code: 42 });").empty());
#endif
}
