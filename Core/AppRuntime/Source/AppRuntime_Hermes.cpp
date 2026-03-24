#include "AppRuntime.h"
#include <napi/env.h>
#include <hermes/hermes.h>

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char*)
    {
        auto hermesRuntime = facebook::hermes::makeHermesRuntime();

        Napi::Env env = Napi::Attach(*hermesRuntime);

        Run(env);

        Napi::Detach(env);
    }
}
