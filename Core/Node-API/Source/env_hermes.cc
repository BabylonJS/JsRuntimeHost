#include <napi/env.h>
#include <napi/js_native_api_types.h>
#include "js_native_api_hermes.h"

namespace Napi
{
    Napi::Env Attach(facebook::hermes::HermesRuntime& runtime)
    {
        napi_env env_ptr{new napi_env__{runtime}};
        return {env_ptr};
    }

    void Detach(Napi::Env env)
    {
        napi_env env_ptr{env};
        delete env_ptr;
    }

    facebook::hermes::HermesRuntime& GetRuntime(Napi::Env env)
    {
        napi_env env_ptr{env};
        return static_cast<facebook::hermes::HermesRuntime&>(env_ptr->runtime);
    }
}
