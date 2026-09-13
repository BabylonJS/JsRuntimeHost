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

        // The Windows 10 Chakra predates ES2020 and has no `globalThis`; scripts written against
        // browsers (and the polyfills in this repo) reference it. Define it as a plain, writable,
        // configurable property of the global object, exactly as the spec describes.
        ThrowIfFailed(JsGetPropertyIdFromName(L"globalThis", &propertyId));
        JsValueRef existingGlobalThis;
        ThrowIfFailed(JsGetProperty(global, propertyId, &existingGlobalThis));
        JsValueType existingType;
        ThrowIfFailed(JsGetValueType(existingGlobalThis, &existingType));
        if (existingType == JsUndefined)
        {
            ThrowIfFailed(JsSetProperty(global, propertyId, global, true));
        }
        ThrowIfFailed(JsGetPropertyIdFromName(L"Object", &propertyId));
        JsValueRef object;
        ThrowIfFailed(JsGetProperty(global, propertyId, &object));
        JsValueRef prototype;
        ThrowIfFailed(JsGetPrototype(object, &prototype));
        ThrowIfFailed(JsGetPropertyIdFromName(L"hasOwnProperty", &propertyId));
        ThrowIfFailed(JsGetProperty(prototype, propertyId, &env_ptr->has_own_property_function));

        JsValueRef wrapSymbolDescription;
        ThrowIfFailed(JsPointerToString(L"BabylonNative_External", 22, &wrapSymbolDescription));
        JsValueRef wrapSymbol;
        ThrowIfFailed(JsCreateSymbol(wrapSymbolDescription, &wrapSymbol));
        ThrowIfFailed(JsAddRef(wrapSymbol, nullptr));
        ThrowIfFailed(JsGetPropertyIdFromSymbol(wrapSymbol, &env_ptr->wrap_property_id));

        return {env_ptr};
    }

    void Detach(Env env)
    {
        napi_env env_ptr{env};
        delete env_ptr;
    }
}
