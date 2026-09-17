#include <napi/env.h>

#include "EvalInternal.h"

namespace Napi
{
    Env Attach(facebook::jsi::Runtime& rt)
    {
        napi_env__* env_ptr{new napi_env__{rt}};
        return {env_ptr};
    }

    void Detach(Env env)
    {
        napi_env__* env_ptr{env};
        delete env_ptr;
    }

    Napi::Value Eval(Napi::Env env, const char* string, const char* sourceUrl)
    {
        napi_env__* env_ptr{env};
        try
        {
            return {env_ptr, env_ptr->rt.evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(string), sourceUrl)};
        }
        catch (const facebook::jsi::JSError& error)
        {
            throw Internal::ConvertEvalException(env, error);
        }
        catch (const facebook::jsi::JSIException& error)
        {
            throw Internal::ConvertEvalException(env, error);
        }
    }
}
