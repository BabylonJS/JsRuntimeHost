#pragma once

#include <Babylon/JsRuntimeScheduler.h>

#include <napi/napi.h>

#include <unordered_map>

class AbortController;

namespace Babylon::Polyfills::Internal
{
    class AbortSignal final : public Napi::ObjectWrap<AbortSignal>
    {
    public:

        static constexpr auto JS_ABORT_SIGNAL_CONSTRUCTOR_NAME = "AbortSignal";

        static void Initialize(Napi::Env env);

        explicit AbortSignal(const Napi::CallbackInfo& info);

    private:

        Napi::Value GetAborted(const Napi::CallbackInfo& info);
        void SetAborted(const Napi::CallbackInfo&, const Napi::Value& value);

        Napi::Value GetOnAbort(const Napi::CallbackInfo& info);
        void SetOnAbort(const Napi::CallbackInfo&, const Napi::Value& value);
        
        //Napi::Value GetReason(const Napi::CallbackInfo& info);

        //void ThrowIfAborted(const Napi::CallbackInfo& info);
        //static Napi::Value Abort(const Napi::CallbackInfo& info);
        //static Napi::Value Timeout(const Napi::CallbackInfo& info);

        void AddEventListener(const Napi::CallbackInfo& info);
        void RemoveEventListener(const Napi::CallbackInfo& info);

        JsRuntimeScheduler m_runtimeScheduler;
        std::unordered_map<std::string, std::vector<Napi::FunctionReference>> m_eventHandlerRefs;

        Napi::FunctionReference m_onabort;

        int m_reason = 0;
        bool m_aborted = false;
    };
}