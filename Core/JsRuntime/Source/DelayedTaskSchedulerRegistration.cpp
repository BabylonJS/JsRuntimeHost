#include "DelayedTaskSchedulerRegistration.h"

#include "JsRuntime.h"

#include <Babylon/DelayedTaskScheduler.h>

namespace Babylon
{
    namespace
    {
        constexpr auto JS_DELAYED_TASK_SCHEDULER_NAME = "delayedTaskScheduler";
    }

    void BABYLON_API DelayedTaskSchedulerRegistration::Register(Napi::Env env, DelayedTaskScheduler& scheduler)
    {
        JsRuntime::NativeObject::GetFromJavaScript(env).Set(
            JS_DELAYED_TASK_SCHEDULER_NAME,
            Napi::External<DelayedTaskScheduler>::New(env, &scheduler));
    }

    void BABYLON_API DelayedTaskSchedulerRegistration::Unregister(Napi::Env env)
    {
        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_DELAYED_TASK_SCHEDULER_NAME, env.Undefined());
    }

    DelayedTaskScheduler* BABYLON_API DelayedTaskSchedulerRegistration::Get(Napi::Env env)
    {
        const auto value = JsRuntime::NativeObject::GetFromJavaScript(env).Get(JS_DELAYED_TASK_SCHEDULER_NAME);
        return value.IsUndefined() ? nullptr : value.As<Napi::External<DelayedTaskScheduler>>().Data();
    }
}
