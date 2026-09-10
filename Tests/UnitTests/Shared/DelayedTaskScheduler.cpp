#include <Babylon/AppRuntime.h>
#include <Babylon/DelayedTaskScheduler.h>
#include <Babylon/DelayedTaskSchedulerRegistration.h>
#include <Babylon/Internal/TimerId.h>
#include <Babylon/Polyfills/Scheduling.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

TEST(TimerId, StartsAtOne)
{
    EXPECT_EQ(Babylon::Internal::IncrementTimerId(0), 1);
}

TEST(TimerId, WrapsBeforeSignedOverflow)
{
    static_assert(Babylon::Internal::IncrementTimerId(std::numeric_limits<int32_t>::max()) == 1);
    auto id = std::numeric_limits<int32_t>::max() - 1;
    id = Babylon::Internal::IncrementTimerId(id);
    EXPECT_EQ(id, std::numeric_limits<int32_t>::max());
    id = Babylon::Internal::IncrementTimerId(id);
    EXPECT_EQ(id, 1);
    id = Babylon::Internal::IncrementTimerId(id);
    EXPECT_EQ(id, 2);
}

TEST(DelayedTaskScheduler, RunsAtOrAfterRequestedTime)
{
    Babylon::DelayedTaskScheduler scheduler;
    std::promise<Babylon::DelayedTaskScheduler::TimePoint> ranPromise;
    const auto requestedTime = std::chrono::time_point_cast<std::chrono::microseconds, std::chrono::steady_clock>(
        std::chrono::steady_clock::now() + 10ms);

    scheduler.Schedule(requestedTime, [&ranPromise]() {
        ranPromise.set_value(std::chrono::time_point_cast<std::chrono::microseconds, std::chrono::steady_clock>(
            std::chrono::steady_clock::now()));
    });

    auto ranFuture = ranPromise.get_future();
    ASSERT_EQ(ranFuture.wait_for(5s), std::future_status::ready);
    EXPECT_GE(ranFuture.get(), requestedTime);
}

TEST(DelayedTaskScheduler, CancelDoesNotWaitForExtractedCallback)
{
    Babylon::DelayedTaskScheduler scheduler;
    std::promise<void> enteredPromise;
    std::promise<void> releasePromise;
    auto release = releasePromise.get_future().share();

    const auto id = scheduler.Schedule(0ms, [&enteredPromise, release]() {
        enteredPromise.set_value();
        release.wait();
    });

    ASSERT_EQ(enteredPromise.get_future().wait_for(5s), std::future_status::ready);
    const auto cancelStarted = std::chrono::steady_clock::now();
    scheduler.Cancel(id);
    EXPECT_LT(std::chrono::steady_clock::now() - cancelStarted, 1s);
    releasePromise.set_value();
}

TEST(DelayedTaskScheduler, CancelRemovesQueuedCallback)
{
    Babylon::DelayedTaskScheduler scheduler;
    std::promise<void> calledPromise;
    auto calledFuture = calledPromise.get_future();

    const auto id = scheduler.Schedule(100ms, [&calledPromise]() {
        calledPromise.set_value();
    });
    scheduler.Cancel(id);

    EXPECT_EQ(calledFuture.wait_for(200ms), std::future_status::timeout);
}

TEST(DelayedTaskScheduler, ShutdownWaitsForRunningCallbackAndRejectsNewWork)
{
    Babylon::DelayedTaskScheduler scheduler;
    std::promise<void> enteredPromise;
    std::promise<void> releasePromise;
    auto release = releasePromise.get_future().share();

    scheduler.Schedule(0ms, [&enteredPromise, release]() {
        enteredPromise.set_value();
        release.wait();
    });
    ASSERT_EQ(enteredPromise.get_future().wait_for(5s), std::future_status::ready);

    auto shutdownFuture = std::async(std::launch::async, [&scheduler]() {
        scheduler.Shutdown();
    });
    EXPECT_EQ(shutdownFuture.wait_for(20ms), std::future_status::timeout);
    releasePromise.set_value();
    EXPECT_EQ(shutdownFuture.wait_for(5s), std::future_status::ready);
    EXPECT_THROW(scheduler.Schedule(0ms, [] {}), std::runtime_error);
}

TEST(DelayedTaskSchedulerRegistration, RegisterAndUnregisterFollowEnvironmentLifetime)
{
    Babylon::AppRuntime runtime;
    std::promise<bool> lifecyclePromise;

    runtime.Dispatch([&lifecyclePromise](Napi::Env env) {
        auto* const scheduler = Babylon::DelayedTaskSchedulerRegistration::Get(env);
        if (scheduler == nullptr)
        {
            lifecyclePromise.set_value(false);
            return;
        }

        Babylon::DelayedTaskSchedulerRegistration::Unregister(env);
        const bool unregistered = Babylon::DelayedTaskSchedulerRegistration::Get(env) == nullptr;
        Babylon::DelayedTaskSchedulerRegistration::Register(env, *scheduler);
        lifecyclePromise.set_value(
            unregistered &&
            Babylon::DelayedTaskSchedulerRegistration::Get(env) == scheduler);
    });

    auto lifecycleFuture = lifecyclePromise.get_future();
    ASSERT_EQ(lifecycleFuture.wait_for(5s), std::future_status::ready);
    EXPECT_TRUE(lifecycleFuture.get());
}

TEST(SchedulingLifecycle, UsesOwnedSchedulerWithoutRegistration)
{
    Babylon::AppRuntime runtime;
    std::promise<void> timerPromise;

    runtime.Dispatch([&timerPromise](Napi::Env env) {
        Babylon::DelayedTaskSchedulerRegistration::Unregister(env);
        Babylon::Polyfills::Scheduling::Initialize(env);
        env.Global().Set(
            "timerComplete",
            Napi::Function::New(env, [&timerPromise](const Napi::CallbackInfo&) {
                timerPromise.set_value();
            }));
        env.Global().Get("setTimeout").As<Napi::Function>().Call(
            env.Global(),
            {env.Global().Get("timerComplete"), Napi::Number::New(env, 1)});
    });

    auto timerFuture = timerPromise.get_future();
    ASSERT_EQ(timerFuture.wait_for(5s), std::future_status::ready);
}

TEST(SchedulingLifecycle, RuntimeShutdownCancelsBorrowedTimers)
{
    std::atomic_bool called{};
    auto destroyFuture = std::async(std::launch::async, [&called]() {
        auto runtime = std::make_unique<Babylon::AppRuntime>();
        std::promise<void> initializedPromise;
        runtime->Dispatch([&called, &initializedPromise](Napi::Env env) {
            Babylon::Polyfills::Scheduling::Initialize(env);
            auto callback = Napi::Function::New(env, [&called](const Napi::CallbackInfo&) {
                called = true;
            });
            env.Global().Get("setTimeout").As<Napi::Function>().Call(
                env.Global(),
                {callback, Napi::Number::New(env, 60000)});
            initializedPromise.set_value();
        });

        if (initializedPromise.get_future().wait_for(5s) != std::future_status::ready)
        {
            throw std::runtime_error{"Scheduling initialization timed out"};
        }
        runtime.reset();
    });

    ASSERT_EQ(destroyFuture.wait_for(5s), std::future_status::ready);
    EXPECT_NO_THROW(destroyFuture.get());
    EXPECT_FALSE(called.load());
}
