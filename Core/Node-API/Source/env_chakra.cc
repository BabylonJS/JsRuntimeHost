#include <napi/env.h>
#include "js_native_api_chakra.h"
#include <jsrt.h>
#include <stdexcept>
#include <strsafe.h>

namespace
{
    void ThrowIfFailed(JsErrorCode errorCode)
    {
        if (errorCode != JsErrorCode::JsNoError)
        {
            throw std::exception();
        }
    }
}

namespace Napi
{
    Env Attach()
    {
        napi_env env_ptr{new napi_env__};

        JsValueRef global;
        ThrowIfFailed(JsGetGlobalObject(&global));
        JsPropertyIdRef propertyId;
        ThrowIfFailed(JsGetPropertyIdFromName(L"Object", &propertyId));
        JsValueRef object;
        ThrowIfFailed(JsGetProperty(global, propertyId, &object));
        JsValueRef prototype;
        ThrowIfFailed(JsGetPrototype(object, &prototype));
        ThrowIfFailed(JsGetPropertyIdFromName(L"hasOwnProperty", &propertyId));
        ThrowIfFailed(JsGetProperty(prototype, propertyId, &env_ptr->has_own_property_function));
        if (napi_shared::CapturePropertyNameIntrinsics(env_ptr, env_ptr->property_name_intrinsics) != napi_ok)
        {
            delete env_ptr;
            throw std::runtime_error{"Napi::Attach: failed to capture property-name intrinsics"};
        }

        JsValueRef wrapSymbolDescription;
        ThrowIfFailed(JsPointerToString(L"BabylonNative_External", 22, &wrapSymbolDescription));
        JsValueRef wrapSymbol;
        ThrowIfFailed(JsCreateSymbol(wrapSymbolDescription, &wrapSymbol));
        ThrowIfFailed(JsAddRef(wrapSymbol, nullptr));
        ThrowIfFailed(JsGetPropertyIdFromSymbol(wrapSymbol, &env_ptr->wrap_property_id));

        return {env_ptr};
    }

    void PrepareForRuntimeDisposal(Env env)
    {
        napi_env env_ptr{env};
        if (napi_shared::ReleasePropertyNameIntrinsics(env_ptr, env_ptr->property_name_intrinsics) != napi_ok)
        {
            throw std::runtime_error{"Napi::PrepareForRuntimeDisposal: failed to release property-name intrinsics"};
        }
    }

    void Detach(Env env)
    {
        napi_env env_ptr{env};
        PrepareForRuntimeDisposal(env);
        delete env_ptr;
    }
}
