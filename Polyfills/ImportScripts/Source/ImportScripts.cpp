#include <Babylon/Polyfills/ImportScripts.h>

#include <stdexcept>
#include <utility>

namespace Babylon::Polyfills::ImportScripts
{
    void BABYLON_API Initialize(Napi::Env env, ResolverT resolver)
    {
        if (!resolver)
        {
            throw std::invalid_argument{"importScripts requires a resource resolver"};
        }

        auto global = env.Global();
        if (!global.Get("importScripts").IsUndefined())
        {
            throw Napi::Error::New(env, "importScripts is already defined");
        }

        if (global.Get("self").IsUndefined())
        {
            global.Set("self", global);
        }

        global.Set("importScripts",
            Napi::Function::New(env, [resolver = std::move(resolver)](const Napi::CallbackInfo& info) {
                for (size_t index = 0; index < info.Length(); ++index)
                {
                    if (!info[index].IsString())
                    {
                        throw Napi::TypeError::New(info.Env(), "importScripts expects resource names as strings");
                    }

                    const auto name = info[index].As<Napi::String>().Utf8Value();
                    std::optional<std::string> source;
                    try
                    {
                        source = resolver(name);
                    }
                    catch (const Napi::Error&)
                    {
                        throw;
                    }
                    catch (const std::exception& error)
                    {
                        throw Napi::Error::New(info.Env(), error.what());
                    }

                    if (!source)
                    {
                        throw Napi::Error::New(info.Env(), "Embedded script not found: " + name);
                    }

                    Napi::Eval(info.Env(), source->c_str(), name.c_str());
                }
            },
                "importScripts"));
    }
}
