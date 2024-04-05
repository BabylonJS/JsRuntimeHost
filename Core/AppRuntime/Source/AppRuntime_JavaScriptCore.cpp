#include "AppRuntime.h"
#include <napi/env.h>

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char*)
    {
        auto globalContext = JSGlobalContextCreateInGroup(nullptr, nullptr);

        JSGlobalContextSetInspectable(globalContext, m_options.EnableDebugger);

        Napi::Env env = Napi::Attach(globalContext);

        Run(env);

        JSGlobalContextRelease(globalContext);

        // Detach must come after JSGlobalContextRelease since it triggers finalizers which require env.
        Napi::Detach(env);
    }
}
