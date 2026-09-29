#include <Babylon/AppRuntime.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/ScriptLoader.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>

TEST(XMLHttpRequest, ThrowingStoppedListenerSkipsRemainingListeners)
{
    auto dispatched = std::make_shared<std::promise<std::string>>();
    auto reportedError = std::make_shared<std::promise<void>>();
    auto errorCount = std::make_shared<std::atomic<int>>(0);
    auto dispatchedFuture = dispatched->get_future();
    auto errorFuture = reportedError->get_future();

    Babylon::AppRuntime::Options options{};
    options.UnhandledExceptionHandler = [reportedError, errorCount](const Napi::Error&) {
        if (errorCount->fetch_add(1) == 0)
        {
            reportedError->set_value();
        }
    };

    Babylon::AppRuntime runtime{options};
    runtime.Dispatch([dispatched](Napi::Env env) {
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        env.Global().Set("reportStoppedListeners", Napi::Function::New(env, [dispatched](const Napi::CallbackInfo& info) {
            dispatched->set_value(info[0].As<Napi::String>().Utf8Value());
        }));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"(
        var xhr = new XMLHttpRequest();
        var invoked = [];
        xhr.addEventListener("load", function (event) {
            invoked.push("first");
            event.stopImmediatePropagation();
            throw new Error("stopped listener failed");
        });
        xhr.addEventListener("load", function () { invoked.push("second"); });
        xhr.addEventListener("loadend", function () {
            reportStoppedListeners(invoked.join(","));
        });
        xhr.open("GET", "app:///Assets/symlink_target.js");
        xhr.send();
    )", "");

    ASSERT_EQ(dispatchedFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(dispatchedFuture.get(), "first");
    ASSERT_EQ(errorFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(errorCount->load(), 1);

    auto drained = std::make_shared<std::promise<void>>();
    auto drainedFuture = drained->get_future();
    runtime.Dispatch([drained](Napi::Env) { drained->set_value(); });
    ASSERT_EQ(drainedFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready);
}
