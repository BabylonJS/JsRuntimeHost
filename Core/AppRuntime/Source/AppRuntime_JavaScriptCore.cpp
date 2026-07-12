#include "AppRuntime.h"
#include <napi/env.h>

#if __ANDROID__
extern "C" void JSCAndroidInitialize();
#endif

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char*)
    {
#if __ANDROID__
        JSCAndroidInitialize();
#endif
        auto globalContext = JSGlobalContextCreateInGroup(nullptr, nullptr);

#if __APPLE__
        if (__builtin_available(iOS 16.4, macOS 13.3, *))
        {
            JSGlobalContextSetInspectable(globalContext, m_options.EnableDebugger);
        }
#endif

        Napi::Env env = Napi::Attach(globalContext);

        Run(env);

#if __ANDROID__
        Napi::ContextLock contextLock{env};
#endif
        JSGlobalContextRelease(globalContext);

        // Detach must come after JSGlobalContextRelease since it triggers finalizers which require env.
        Napi::Detach(env);
    }

    void AppRuntime::DrainMicrotasks(Napi::Env)
    {
        // JavaScriptCore drains microtasks automatically at script boundaries.
    }
}
