#include <Babylon/AppRuntime.h>
#include <Babylon/JsRuntime.h>
#include <Babylon/Polyfills/DynamicScriptLoader.h>
#include <Babylon/ScriptLoader.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stdexcept>
#include <string>

TEST(DynamicScriptLoader, SupportsImmediateAndDeferredResources)
{
    std::promise<std::string> result;
    Babylon::AppRuntime runtime{};
    runtime.Dispatch([&result](Napi::Env env) {
        Babylon::Polyfills::DynamicScriptLoader::Initialize(env, [](Napi::Env env, const std::string& name) -> Napi::Value {
            if (name == "immediate.js")
            {
                return Napi::String::New(env, "window.loaded = 1;");
            }
            if (name == "deferred.js")
            {
                auto deferred = Napi::Promise::Deferred::New(env);
                Babylon::JsRuntime::GetFromJavaScript(env).Dispatch([deferred](Napi::Env callbackEnv) {
                    deferred.Resolve(Napi::String::New(callbackEnv, "window.loaded += 41;"));
                });
                return deferred.Promise();
            }
            return env.Null();
        });
        env.Global().Set("reportResult", Napi::Function::New(env, [&result](const Napi::CallbackInfo& info) {
            result.set_value(info[0].As<Napi::String>().Utf8Value());
        }));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"(
        loadScript("immediate.js")
            .then(function () { return loadScript("deferred.js"); })
            .then(function () { return loadScript("missing.js"); })
            .then(function () { reportResult("missing script loaded"); },
                  function (error) { reportResult(window.loaded + "|" + error.message); })
            .catch(function (error) { reportResult("unexpected: " + error.message); });
    )", "entry.js");

    auto future = result.get_future();
    ASSERT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(future.get(), "42|Script source not found: missing.js");
}

TEST(DynamicScriptLoader, RejectsResolverAndScriptErrors)
{
    std::promise<std::string> result;
    Babylon::AppRuntime runtime{};
    runtime.Dispatch([&result](Napi::Env env) {
        Babylon::Polyfills::DynamicScriptLoader::Initialize(env, [](Napi::Env env, const std::string& name) -> Napi::Value {
            if (name == "broken.js")
            {
                return Napi::String::New(env, "throw new Error('script failed')");
            }
            if (name == "rejected.js")
            {
                return Napi::Eval(env, "Promise.reject(new Error('lookup failed'))", "resolver.js");
            }
            throw std::runtime_error{"resolver failed"};
        });
        env.Global().Set("reportResult", Napi::Function::New(env, [&result](const Napi::CallbackInfo& info) {
            result.set_value(info[0].As<Napi::String>().Utf8Value());
        }));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"(
        Promise.all([
            loadScript("broken.js").then(null, function (error) { return error.message; }),
            loadScript("rejected.js").then(null, function (error) { return error.message; }),
            loadScript("throw.js").then(null, function (error) { return error.message; }),
            loadScript().then(null, function (error) { return error.name; })
        ]).then(function (errors) { reportResult(errors.join("|")); });
    )", "entry.js");

    auto future = result.get_future();
    ASSERT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(future.get(), "script failed|lookup failed|resolver failed|TypeError");
}
