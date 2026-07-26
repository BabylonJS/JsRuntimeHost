#include "AppRuntime.h"
#include <napi/env.h>

#if __has_include(<JavaScriptCore/JSContextRefPrivate.h>)
#include <JavaScriptCore/JSContextRefPrivate.h>
#define JSRUNTIMEHOST_HAS_JSC_EXECUTION_TIME_LIMIT 1
#elif defined(__unix__)
#include <dlfcn.h>
#define JSRUNTIMEHOST_LOOKUP_JSC_EXECUTION_TIME_LIMIT 1
#endif

namespace
{
#if defined(JSRUNTIMEHOST_LOOKUP_JSC_EXECUTION_TIME_LIMIT)
    using SetExecutionTimeLimit = void (*)(JSContextGroupRef, double, bool (*)(JSContextRef, void*), void*);
    using ClearExecutionTimeLimit = void (*)(JSContextGroupRef);
#endif
}

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char*)
    {
        auto globalContext = JSGlobalContextCreateInGroup(nullptr, nullptr);

#if defined(JSRUNTIMEHOST_HAS_JSC_EXECUTION_TIME_LIMIT) || \
    defined(JSRUNTIMEHOST_LOOKUP_JSC_EXECUTION_TIME_LIMIT)
        auto contextGroup = JSContextGetGroup(globalContext);
        const auto shouldTerminateJSC = [](JSContextRef, void* context) {
            return static_cast<AppRuntime*>(context)->IsExecutionTerminationRequested();
        };
#endif

#if defined(JSRUNTIMEHOST_HAS_JSC_EXECUTION_TIME_LIMIT)
        // Poll at a modest interval while JS is running. Returning true from
        // this callback raises a catchable termination exception and lets the
        // AppRuntime thread unwind, so Worker::terminate() also stops a tight
        // loop that never reaches the dispatch queue.
        JSContextGroupSetExecutionTimeLimit(
            contextGroup,
            0.05,
            shouldTerminateJSC,
            this);
#elif defined(JSRUNTIMEHOST_LOOKUP_JSC_EXECUTION_TIME_LIMIT)
        // WebKitGTK deliberately omits JSContextRefPrivate.h from its dev
        // package, but current system builds export the same C ABI. Resolve it
        // dynamically so JsRuntimeHost stays buildable against the public
        // package and gracefully falls back to between-dispatch termination on
        // older builds that do not export the watchdog.
        auto setExecutionTimeLimit = reinterpret_cast<SetExecutionTimeLimit>(
            dlsym(RTLD_DEFAULT, "JSContextGroupSetExecutionTimeLimit"));
        auto clearExecutionTimeLimit = reinterpret_cast<ClearExecutionTimeLimit>(
            dlsym(RTLD_DEFAULT, "JSContextGroupClearExecutionTimeLimit"));
        if (setExecutionTimeLimit != nullptr && clearExecutionTimeLimit != nullptr)
        {
            setExecutionTimeLimit(contextGroup, 0.05, shouldTerminateJSC, this);
        }
#endif

#if __APPLE__
        if (__builtin_available(iOS 16.4, macOS 13.3, *))
        {
            JSGlobalContextSetInspectable(globalContext, m_options.EnableDebugger);
        }
#endif

        Napi::Env env = Napi::Attach(globalContext);

        Run(env);

#if defined(JSRUNTIMEHOST_HAS_JSC_EXECUTION_TIME_LIMIT)
        JSContextGroupClearExecutionTimeLimit(contextGroup);
#elif defined(JSRUNTIMEHOST_LOOKUP_JSC_EXECUTION_TIME_LIMIT)
        if (setExecutionTimeLimit != nullptr && clearExecutionTimeLimit != nullptr)
        {
            clearExecutionTimeLimit(contextGroup);
        }
#endif

        JSGlobalContextRelease(globalContext);

        // Detach must come after JSGlobalContextRelease since it triggers finalizers which require env.
        Napi::Detach(env);
    }

    void AppRuntime::ShutdownEnvironment(Napi::Env)
    {
    }

    void AppRuntime::DrainMicrotasks(Napi::Env)
    {
        // JavaScriptCore drains microtasks automatically at script boundaries.
    }
}
