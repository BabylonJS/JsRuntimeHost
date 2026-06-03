#include <napi/env.h>
#include "js_native_api_chakra.h"
#include <jsrt.h>
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

        JsValueRef wrapInfoDescription;
        ThrowIfFailed(JsPointerToString(L"BabylonNative_External", 22, &wrapInfoDescription));
        ThrowIfFailed(JsCreateSymbol(wrapInfoDescription, &env_ptr->wrap_info_symbol));
        ThrowIfFailed(JsAddRef(env_ptr->wrap_info_symbol, nullptr));
        ThrowIfFailed(JsGetPropertyIdFromSymbol(env_ptr->wrap_info_symbol, &env_ptr->wrap_info_property_id));

        return {env_ptr};
    }

    void Detach(Env env)
    {
        napi_env env_ptr{env};
        delete env_ptr;
    }
}
