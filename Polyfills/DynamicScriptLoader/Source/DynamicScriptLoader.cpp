#include <Babylon/Polyfills/DynamicScriptLoader.h>

#include <stdexcept>
#include <utility>

namespace Babylon::Polyfills::DynamicScriptLoader
{
    void BABYLON_API Initialize(Napi::Env env, ResolverT resolver)
    {
        if (!resolver)
        {
            throw std::invalid_argument{"loadScript requires a resource resolver"};
        }

        auto global = env.Global();
        if (!global.Get("loadScript").IsUndefined())
        {
            throw Napi::Error::New(env, "loadScript is already defined");
        }

        global.Set("loadScript",
            Napi::Function::New(env, [resolver = std::move(resolver)](const Napi::CallbackInfo& info) -> Napi::Value {
                auto env = info.Env();
                const auto deferred = Napi::Promise::Deferred::New(env);
                if (info.Length() != 1 || !info[0].IsString())
                {
                    deferred.Reject(Napi::TypeError::New(env, "loadScript expects one resource name").Value());
                    return deferred.Promise();
                }

                const auto name = info[0].As<Napi::String>().Utf8Value();
                try
                {
                    auto source = resolver(env, name);
                    auto promise = env.Global().Get("Promise").As<Napi::Function>();
                    auto resolve = promise.Get("resolve").As<Napi::Function>();
                    auto resolved = resolve.Call(promise, {source}).As<Napi::Promise>();
                    auto evaluate = Napi::Function::New(env, [name](const Napi::CallbackInfo& callback) {
                        if (!callback[0].IsString())
                        {
                            throw Napi::TypeError::New(callback.Env(), "Script source not found: " + name);
                        }
                        auto text = callback[0].As<Napi::String>().Utf8Value();
                        Napi::Eval(callback.Env(), text.c_str(), name.c_str());
                    });
                    return resolved.Get("then").As<Napi::Function>().Call(resolved, {evaluate});
                }
                catch (const Napi::Error& error)
                {
                    deferred.Reject(error.Value());
                }
                catch (const std::exception& error)
                {
                    deferred.Reject(Napi::Error::New(env, error.what()).Value());
                }
                return deferred.Promise();
            },
                "loadScript"));
    }
}
