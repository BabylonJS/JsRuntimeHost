#pragma once

#include <Babylon/JsRuntimeScheduler.h>

#include <napi/napi.h>
#include <UrlLib/UrlLib.h>

#include <unordered_map>
#include <vector>

namespace Babylon::Polyfills::Internal
{
    class XMLHttpRequest final : public Napi::ObjectWrap<XMLHttpRequest>
    {
    public:
        static void Initialize(Napi::Env env);

        explicit XMLHttpRequest(const Napi::CallbackInfo& info);

    private:
        enum class ReadyState
        {
            Unsent = 0,
            Opened = 1,
            Done = 4,
        };

        Napi::Value GetReadyState(const Napi::CallbackInfo& info);
        Napi::Value GetResponse(const Napi::CallbackInfo& info);
        Napi::Value GetResponseText(const Napi::CallbackInfo& info);
        Napi::Value GetResponseType(const Napi::CallbackInfo& info);
        void SetResponseType(const Napi::CallbackInfo& info, const Napi::Value& value);
        Napi::Value GetResponseHeader(const Napi::CallbackInfo& info);
        void SetRequestHeader(const Napi::CallbackInfo& info);
        Napi::Value GetAllResponseHeaders(const Napi::CallbackInfo& info);
        Napi::Value GetResponseURL(const Napi::CallbackInfo& info);
        Napi::Value GetStatus(const Napi::CallbackInfo& info);
        Napi::Value GetStatusText(const Napi::CallbackInfo& info);
        Napi::Value GetErrorCode(const Napi::CallbackInfo& info);
        Napi::Value GetErrorDetail(const Napi::CallbackInfo& info);

        // Indices into XMLHttpRequest::EVENT_TYPE_NAMES; used to instantiate the `on<event>`
        // property accessors below without needing a distinct method per event type.
        enum class EventIndex : size_t
        {
            ReadyStateChange = 0,
            Load = 1,
            Error = 2,
            LoadEnd = 3,
            Abort = 4,
            Count = 5,
        };

        static const char* const EVENT_TYPE_NAMES[static_cast<size_t>(EventIndex::Count)];

        template<EventIndex Index> Napi::Value GetEventHandler(const Napi::CallbackInfo& info);
        template<EventIndex Index> void SetEventHandler(const Napi::CallbackInfo& info, const Napi::Value& value);

        void AddEventListener(const Napi::CallbackInfo& info);
        void RemoveEventListener(const Napi::CallbackInfo& info);
        void Abort(const Napi::CallbackInfo& info);
        void Open(const Napi::CallbackInfo& info);
        void Send(const Napi::CallbackInfo& info);

        void SetReadyState(ReadyState readyState);
        void RaiseEvent(const char* eventType);

        // A registered event listener. `isEventHandler` marks the single entry owned by the
        // matching `on<event>` property; every other entry came from addEventListener. Both
        // kinds share one list per event type because that is what the DOM specifies: dispatch
        // follows registration order, so `addEventListener("load", a)` then `xhr.onload = b`
        // calls `a` then `b`, and reassigning `onload` keeps its original position rather than
        // moving to the end ("If eventHandler's listener is not null, then return").
        struct Listener
        {
            Napi::FunctionReference callback;
            bool isEventHandler;
        };

        std::string m_url{};
        UrlLib::UrlRequest m_request{};
        JsRuntimeScheduler m_runtimeScheduler;
        ReadyState m_readyState{ReadyState::Unsent};
        // Set by abort(); makes the in-flight continuation report 'abort' instead of 'error'.
        bool m_aborted{false};
        std::unordered_map<std::string, std::vector<Listener>> m_listeners;
    };
}
