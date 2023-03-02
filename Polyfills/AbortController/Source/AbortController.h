#pragma once

#include <Babylon/JsRuntimeScheduler.h>

#include <napi/napi.h>
#include <../../AbortSignal/Source/AbortSignal.h>

namespace Babylon::Polyfills::Internal
{
    class AbortController final : public Napi::ObjectWrap<AbortController>
    {
    public:
        static void Initialize(Napi::Env env);

        explicit AbortController(const Napi::CallbackInfo& info);

    private:
        Napi::Value GetSignal(const Napi::CallbackInfo& info);

        void Abort(const Napi::CallbackInfo& info);

        JsRuntimeScheduler m_runtimeScheduler;
        std::unordered_map<std::string, std::vector<Napi::FunctionReference>> m_eventHandlerRefs;
        Napi::ObjectReference m_signal;
    };
}