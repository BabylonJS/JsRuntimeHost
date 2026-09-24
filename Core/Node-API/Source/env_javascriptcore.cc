#include <napi/env.h>
#include <napi/js_native_api_types.h>
#include "js_native_api_javascriptcore.h"
#include <stdexcept>

namespace Napi
{
    Napi::Env Attach(JSGlobalContextRef context)
    {
        napi_env env_ptr{new napi_env__{context}};
        if (napi_shared::CapturePropertyNameIntrinsics(env_ptr, env_ptr->property_name_intrinsics) != napi_ok)
        {
            delete env_ptr;
            throw std::runtime_error{"Napi::Attach: failed to capture property-name intrinsics"};
        }
        return {env_ptr};
    }

    void Detach(Napi::Env env)
    {
        napi_env env_ptr{env};
        if (napi_shared::ReleasePropertyNameIntrinsics(env_ptr, env_ptr->property_name_intrinsics) != napi_ok)
        {
            throw std::runtime_error{"Napi::Detach: failed to release property-name intrinsics"};
        }
        delete env_ptr;
    }

    JSGlobalContextRef GetContext(Napi::Env env)
    {
        napi_env env_ptr{env};
        return env_ptr->context;
    }
}
