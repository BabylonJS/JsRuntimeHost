#include "AppRuntime.h"
#include <napi/env.h>

#include <quickjs.h>

#include <optional>

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char* executablePath)
    {
        // Create the runtime.
        JSRuntime* runtime = JS_NewRuntime();
        if (!runtime)
        {
            throw std::runtime_error{"Failed to create QuickJS runtime"};
        }

        // Create the context.
        JSContext* context = JS_NewContext(runtime);
        if (!context)
        {
            JS_FreeRuntime(runtime);
            throw std::runtime_error{"Failed to create QuickJS context"};
        }

        // Use the context within a scope.
        {
            Napi::Env env = Napi::Attach(context);

            Run(env);

            Napi::Detach(env);
        }

        // Destroy the context and runtime.
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
    }
}
