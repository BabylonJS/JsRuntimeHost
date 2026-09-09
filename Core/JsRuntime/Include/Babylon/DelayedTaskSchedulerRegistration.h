#pragma once

#include <Babylon/Api.h>
#include <napi/env.h>

namespace Babylon
{
    class DelayedTaskScheduler;

    class DelayedTaskSchedulerRegistration final
    {
    public:
        // Called on the JavaScript thread after JsRuntime initialization. The
        // host retains ownership and must outlive all borrowers in the environment.
        static void BABYLON_API Register(Napi::Env env, DelayedTaskScheduler& scheduler);
        static void BABYLON_API Unregister(Napi::Env env);
        static DelayedTaskScheduler* BABYLON_API Get(Napi::Env env);
    };
}
