#include "AppRuntime.h"
#include "AppRuntime_PromiseRejection.h"
#include <napi/env.h>

// JSGlobalContextSetUnhandledRejectionCallback is declared in the private JavaScriptCore header
// <JavaScriptCore/JSContextRefPrivate.h>, which is not part of the public macOS/iOS SDK. The symbol
// is exported by the JavaScriptCore framework (SPI), so it is forward-declared here and the call is
// guarded with __builtin_available (it is JSC_API_AVAILABLE(macos(10.15.4), ios(13.4))). It registers
// a JS function invoked at the microtask checkpoint with (promise, reason) for each promise that is
// still unhandled at that point, so -- unlike V8 -- no deferral or candidate bookkeeping is needed.
extern "C" void JSGlobalContextSetUnhandledRejectionCallback(JSGlobalContextRef ctx, JSObjectRef function, JSValueRef* exception);

namespace Babylon
{
    namespace
    {
        // JSObjectMakeFunctionWithCallback takes no user-data argument; each AppRuntime owns its JSC
        // context on a dedicated thread, so a thread_local associates the callback with this runtime.
        struct JSCRejectionContext
        {
            AppRuntime* runtime{};
            napi_env env{};
        };

        thread_local JSCRejectionContext* t_rejectionContext{nullptr};

        // Mirrors ToNapi (js_native_api_javascriptcore.cc): napi_value is a JSValueRef in the
        // JavaScriptCore Node-API shim.
        napi_value JsValueToNapi(JSValueRef value)
        {
            return reinterpret_cast<napi_value>(const_cast<OpaqueJSValue*>(value));
        }

        JSValueRef OnUnhandledRejection(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argumentCount, const JSValueRef arguments[], JSValueRef*)
        {
            JSCRejectionContext* context{t_rejectionContext};
            if (context != nullptr && argumentCount >= 2)
            {
                const Napi::Env env{context->env};
                context->runtime->OnUnhandledPromiseRejection(Internal::ToError(env, JsValueToNapi(arguments[1])));
            }

            return JSValueMakeUndefined(ctx);
        }
    }

    void AppRuntime::RunEnvironmentTier(const char*)
    {
        auto globalContext = JSGlobalContextCreateInGroup(nullptr, nullptr);

#if __APPLE__
        if (__builtin_available(iOS 16.4, macOS 13.3, *))
        {
            JSGlobalContextSetInspectable(globalContext, m_options.EnableDebugger);
        }
#endif

        Napi::Env env = Napi::Attach(globalContext);

        // Always track unhandled promise rejections (routed to the host UnhandledExceptionHandler).
        JSCRejectionContext rejectionContext{this, env};
        t_rejectionContext = &rejectionContext;
        if (__builtin_available(iOS 13.4, macOS 10.15.4, *))
        {
            JSStringRef callbackName = JSStringCreateWithUTF8CString("onUnhandledRejection");
            JSObjectRef callback = JSObjectMakeFunctionWithCallback(globalContext, callbackName, OnUnhandledRejection);
            JSStringRelease(callbackName);
            JSGlobalContextSetUnhandledRejectionCallback(globalContext, callback, nullptr);
        }

        Run(env);

        t_rejectionContext = nullptr;

        JSGlobalContextRelease(globalContext);

        // Detach must come after JSGlobalContextRelease since it triggers finalizers which require env.
        Napi::Detach(env);
    }
}
