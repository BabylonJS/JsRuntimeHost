#include "AbortController.h"
#include <cassert>

namespace Babylon::Polyfills::Internal
{
    void AbortController::Initialize(Napi::Env env)
    {
        static constexpr auto JS_ABORT_CONTROLLER_CONSTRUCTOR_NAME = "AbortController";
        if (env.Global().Get(JS_ABORT_CONTROLLER_CONSTRUCTOR_NAME).IsUndefined())
        {
            Napi::Function func = DefineClass(
                env,
                JS_ABORT_CONTROLLER_CONSTRUCTOR_NAME,
                {
                    InstanceAccessor("signal", &AbortController::GetSignal, nullptr),
                    InstanceMethod("abort", &AbortController::Abort),
                });

            env.Global().Set(JS_ABORT_CONTROLLER_CONSTRUCTOR_NAME, func);
        }
    }

    Napi::Value AbortController::GetSignal(const Napi::CallbackInfo&)
    {
        return m_signal.Value();
    }

    void AbortController::Abort(const Napi::CallbackInfo&)
    {
        AbortSignal* sig = Babylon::Polyfills::Internal::AbortSignal::Unwrap(m_signal.Value());
        
        assert(sig != nullptr);
        sig->Abort();
    }

    AbortController::AbortController(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<Babylon::Polyfills::Internal::AbortController>{info}
    {
        m_signal = Napi::Persistent(info.Env().Global().Get(Babylon::Polyfills::Internal::AbortSignal::JS_ABORT_SIGNAL_CONSTRUCTOR_NAME).As<Napi::Function>().New({}));
    }
}

namespace Babylon::Polyfills::AbortController
{
    void BABYLON_API Initialize(Napi::Env env)
    {
        Babylon::Polyfills::Internal::AbortController::Initialize(env);
        Babylon::Polyfills::Internal::AbortSignal::Initialize(env);
    }
}