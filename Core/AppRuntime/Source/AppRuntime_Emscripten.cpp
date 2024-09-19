#include "AppRuntime.h"
#include <napi/env.h>

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char*)
    {
        Napi::Env env = Napi::Attach();

        Run(env);

        Napi::Detach(env);
    }
} // namespace Babylon
