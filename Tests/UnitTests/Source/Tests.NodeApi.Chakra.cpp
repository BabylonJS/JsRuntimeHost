#include <Babylon/AppRuntime.h>
#include <gtest/gtest.h>
#include <jsrt.h>
#include <future>

TEST(NodeApi, CachedHasOwnPropertySurvivesReplacementAndCollection)
{
    Babylon::AppRuntime runtime{};
    std::promise<bool> result;
    runtime.Dispatch([&result](Napi::Env env) {
        napi_env rawEnv{env};
        Napi::Object prototype{env.Global().Get("Object").As<Napi::Function>().Get("prototype").As<Napi::Object>()};
        prototype.Set("hasOwnProperty", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
            return Napi::Boolean::New(info.Env(), false);
        }));

        JsContextRef context{};
        JsRuntimeHandle jsRuntime{};
        if (JsGetCurrentContext(&context) != JsNoError ||
            JsGetRuntime(context, &jsRuntime) != JsNoError ||
            JsCollectGarbage(jsRuntime) != JsNoError)
        {
            result.set_value(false);
            return;
        }

        Napi::Object object{Napi::Object::New(env)};
        object.Set("owned", true);
        Napi::String key{Napi::String::New(env, "owned")};
        bool hasOwn{};
        result.set_value(napi_has_own_property(rawEnv, object, key, &hasOwn) == napi_ok && hasOwn);
    });
    EXPECT_TRUE(result.get_future().get());
}
