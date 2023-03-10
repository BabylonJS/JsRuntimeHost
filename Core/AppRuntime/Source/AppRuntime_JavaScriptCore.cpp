#include "AppRuntime.h"
#include <napi/env_javascriptcore.h>

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char*)
    {
        auto globalContext = JSGlobalContextCreateInGroup(nullptr, nullptr);
        Napi::Env env = Napi::JavaScriptCore::Attach(globalContext);

        Run(env);

        JSGlobalContextRelease(globalContext);

        // Detach must come after JSGlobalContextRelease since it triggers finalizers which require env.
        Napi::JavaScriptCore::Detach(env);
    }
}
