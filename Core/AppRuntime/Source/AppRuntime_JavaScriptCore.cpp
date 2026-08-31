#include "AppRuntime.h"
#include <napi/env.h>

#if defined(JSR_USE_BUN_JSC)
extern "C" void JSCBunInitialize();
#endif

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char*)
    {
#if defined(JSR_USE_BUN_JSC)
        JSCBunInitialize();
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

#if defined(JSR_USE_BUN_JSC)
        {
            // Scope this holder to the host's context reference. Keeping it alive across Detach
            // delays VM destruction until after napi_env has been deleted, but JSC's last-chance
            // finalizers are allowed to call Node-API with that environment.
            Napi::ContextLock contextLock{env};
#endif
            JSGlobalContextRelease(globalContext);
#if defined(JSR_USE_BUN_JSC)
        }
#endif

        // Detach must come after JSGlobalContextRelease since it triggers finalizers which require env.
        Napi::Detach(env);
    }

    void AppRuntime::DrainMicrotasks(Napi::Env)
    {
        // JavaScriptCore drains microtasks automatically at script boundaries.
    }
}
