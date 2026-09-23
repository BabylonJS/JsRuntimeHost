#include "XMLHttpRequest.h"
#include <Babylon/JsRuntime.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <arcana/tracing/trace_region.h>
#include <algorithm>
#include <sstream>
#include <string_view>

namespace Babylon::Polyfills::Internal
{
    namespace
    {
        namespace ResponseType
        {
            constexpr const char* Text = "text";
            constexpr const char* ArrayBuffer = "arraybuffer";

            UrlLib::UrlResponseType StringToEnum(const std::string& value)
            {
                if (value == Text)
                    return UrlLib::UrlResponseType::String;
                if (value == ArrayBuffer)
                    return UrlLib::UrlResponseType::Buffer;

                throw std::runtime_error{"Unsupported response type: " + value};
            }

            const char* EnumToString(UrlLib::UrlResponseType value)
            {
                switch (value)
                {
                    case UrlLib::UrlResponseType::String:
                        return Text;
                    case UrlLib::UrlResponseType::Buffer:
                        return ArrayBuffer;
                }

                throw std::runtime_error{"Invalid response type"};
            }
        }

        namespace MethodType
        {
            constexpr const char* Get = "GET";
            constexpr const char* Post = "POST";

            UrlLib::UrlMethod StringToEnum(const std::string& value)
            {
                if (value == Get)
                    return UrlLib::UrlMethod::Get;
                else if (value == Post)
                    return UrlLib::UrlMethod::Post;

                throw std::runtime_error{"Unsupported url method: " + value};
            }
        }

        namespace EventType
        {
            constexpr const char* ReadyStateChange = "readystatechange";
            constexpr const char* LoadEnd = "loadend";
            constexpr const char* Error = "error";
            constexpr const char* Load = "load";
            constexpr const char* Abort = "abort";
        }

        constexpr const char* EVENT_FACTORY_NAME = "__jsRuntimeHostMakeXHREvent";
        constexpr const char* EVENT_FACTORY_SOURCE = R"JS(
            (function (global) {
                if (typeof global.Event !== "function") {
                    function Event(type, init) {
                        init = init || {};
                        this.type = String(type);
                        this.bubbles = !!init.bubbles;
                        this.cancelable = !!init.cancelable;
                        this.composed = !!init.composed;
                        this.defaultPrevented = false;
                        this.target = null;
                        this.currentTarget = null;
                        this.eventPhase = 0;
                        this.timeStamp = Date.now();
                        this.isTrusted = false;
                        this.cancelBubble = false;
                    }
                    Event.prototype.preventDefault = function () {
                        if (this.cancelable) this.defaultPrevented = true;
                    };
                    Event.prototype.stopPropagation = function () { this.cancelBubble = true; };
                    Event.prototype.stopImmediatePropagation = function () { this.cancelBubble = true; };
                    Event.prototype.composedPath = function () {
                        return this.currentTarget === null ? [] : [this.target];
                    };
                    Event.NONE = Event.prototype.NONE = 0;
                    Event.CAPTURING_PHASE = Event.prototype.CAPTURING_PHASE = 1;
                    Event.AT_TARGET = Event.prototype.AT_TARGET = 2;
                    Event.BUBBLING_PHASE = Event.prototype.BUBBLING_PHASE = 3;
                    global.Event = Event;
                }

                if (typeof global.ProgressEvent !== "function") {
                    function ProgressEvent(type, init) {
                        init = init || {};
                        var event = new global.Event(type, init);
                        Object.setPrototypeOf(event, ProgressEvent.prototype);
                        event.lengthComputable = !!init.lengthComputable;
                        event.loaded = Number(init.loaded || 0);
                        event.total = Number(init.total || 0);
                        return event;
                    }
                    ProgressEvent.prototype = Object.create(global.Event.prototype);
                    ProgressEvent.prototype.constructor = ProgressEvent;
                    global.ProgressEvent = ProgressEvent;
                }

                return function (type, target, progress) {
                    var event = progress
                        ? new global.ProgressEvent(type, {lengthComputable: false, loaded: 0, total: 0})
                        : new global.Event(type);
                    var currentTarget = target;
                    var eventPhase = 2;
                    Object.defineProperties(event, {
                        target: {value: target, configurable: true},
                        currentTarget: {get: function () { return currentTarget; }, configurable: true},
                        eventPhase: {get: function () { return eventPhase; }, configurable: true}
                    });
                    var stopped = false;
                    var stop = event.stopImmediatePropagation;
                    Object.defineProperty(event, "stopImmediatePropagation", {
                        configurable: true,
                        value: function () {
                            stopped = true;
                            return stop.call(this);
                        }
                    });
                    return {
                        value: event,
                        isStopped: function () { return stopped; },
                        end: function () {
                            currentTarget = null;
                            eventPhase = 0;
                        }
                    };
                };
            })(typeof globalThis === "object" ? globalThis : this)
        )JS";
    }

    const char* const XMLHttpRequest::EVENT_TYPE_NAMES[static_cast<size_t>(XMLHttpRequest::EventIndex::Count)] = {
        EventType::ReadyStateChange,
        EventType::Load,
        EventType::Error,
        EventType::LoadEnd,
        EventType::Abort,
    };

    template<XMLHttpRequest::EventIndex Index>
    Napi::Value XMLHttpRequest::GetEventHandler(const Napi::CallbackInfo&)
    {
        const auto it = m_listeners.find(EVENT_TYPE_NAMES[static_cast<size_t>(Index)]);
        if (it != m_listeners.end())
        {
            for (const auto& listener : it->second)
            {
                if (listener->active && listener->isEventHandler)
                {
                    return listener->callback.Value();
                }
            }
        }

        return Env().Null();
    }

    template<XMLHttpRequest::EventIndex Index>
    void XMLHttpRequest::SetEventHandler(const Napi::CallbackInfo&, const Napi::Value& value)
    {
        auto& listeners = m_listeners[EVENT_TYPE_NAMES[static_cast<size_t>(Index)]];
        const auto it = std::find_if(listeners.begin(), listeners.end(), [](const std::shared_ptr<Listener>& listener) {
            return listener->isEventHandler;
        });

        // Event handler attributes treat primitive values as null. Object values are retained
        // verbatim for the getter; non-callable objects are simply skipped during dispatch.
        if (!value.IsObject())
        {
            if (it != listeners.end())
            {
                (*it)->active = false;
                listeners.erase(it);
            }

            return;
        }

        if (it != listeners.end())
        {
            // Replace in place so reassignment keeps this listener's position in the
            // dispatch order.
            (*it)->callback = Napi::Persistent(value.As<Napi::Object>());
        }
        else
        {
            listeners.push_back(std::make_shared<Listener>(Listener{Napi::Persistent(value.As<Napi::Object>()), true}));
        }
    }

    void XMLHttpRequest::Initialize(Napi::Env env)
    {
        static constexpr auto JS_XML_HTTP_REQUEST_CONSTRUCTOR_NAME = "XMLHttpRequest";

        Napi::Function func = DefineClass(
            env,
            JS_XML_HTTP_REQUEST_CONSTRUCTOR_NAME,
            {
                StaticValue("UNSENT", Napi::Value::From(env, 0)),
                StaticValue("OPENED", Napi::Value::From(env, 1)),
                StaticValue("HEADERS_RECEIVED", Napi::Value::From(env, 2)),
                StaticValue("LOADING", Napi::Value::From(env, 3)),
                StaticValue("DONE", Napi::Value::From(env, 4)),
                InstanceAccessor("readyState", &XMLHttpRequest::GetReadyState, nullptr),
                InstanceAccessor("response", &XMLHttpRequest::GetResponse, nullptr),
                InstanceAccessor("responseText", &XMLHttpRequest::GetResponseText, nullptr),
                InstanceAccessor("responseType", &XMLHttpRequest::GetResponseType, &XMLHttpRequest::SetResponseType),
                InstanceAccessor("responseURL", &XMLHttpRequest::GetResponseURL, nullptr),
                InstanceAccessor("status", &XMLHttpRequest::GetStatus, nullptr),
                InstanceAccessor("statusText", &XMLHttpRequest::GetStatusText, nullptr),
                // Non-standard, additive diagnostics: the normalized transport-error detail from
                // UrlLib, empty unless the request failed at the transport layer. Browsers do not
                // expose these, so spec-conformant code is unaffected; BN-aware code can read them
                // to tell a DNS failure from a refused connection or a missing local asset.
                InstanceAccessor("errorCode", &XMLHttpRequest::GetErrorCode, nullptr),
                InstanceAccessor("errorDetail", &XMLHttpRequest::GetErrorDetail, nullptr),
                // DOM `on<event>` handler properties. Without these, `xhr.onreadystatechange = fn`
                // silently sets an ordinary expando property that is never invoked, so code written
                // against the standard XMLHttpRequest API waits forever for a callback that can
                // never fire.
                InstanceAccessor("onreadystatechange", &XMLHttpRequest::GetEventHandler<EventIndex::ReadyStateChange>, &XMLHttpRequest::SetEventHandler<EventIndex::ReadyStateChange>),
                InstanceAccessor("onload", &XMLHttpRequest::GetEventHandler<EventIndex::Load>, &XMLHttpRequest::SetEventHandler<EventIndex::Load>),
                InstanceAccessor("onerror", &XMLHttpRequest::GetEventHandler<EventIndex::Error>, &XMLHttpRequest::SetEventHandler<EventIndex::Error>),
                InstanceAccessor("onloadend", &XMLHttpRequest::GetEventHandler<EventIndex::LoadEnd>, &XMLHttpRequest::SetEventHandler<EventIndex::LoadEnd>),
                InstanceAccessor("onabort", &XMLHttpRequest::GetEventHandler<EventIndex::Abort>, &XMLHttpRequest::SetEventHandler<EventIndex::Abort>),
                InstanceMethod("getAllResponseHeaders", &XMLHttpRequest::GetAllResponseHeaders),
                InstanceMethod("getResponseHeader", &XMLHttpRequest::GetResponseHeader),
                InstanceMethod("setRequestHeader", &XMLHttpRequest::SetRequestHeader),
                InstanceMethod("addEventListener", &XMLHttpRequest::AddEventListener),
                InstanceMethod("removeEventListener", &XMLHttpRequest::RemoveEventListener),
                InstanceMethod("abort", &XMLHttpRequest::Abort),
                InstanceMethod("open", &XMLHttpRequest::Open),
                InstanceMethod("send", &XMLHttpRequest::Send),
            });

        auto eventFactory = Napi::Eval(env, EVENT_FACTORY_SOURCE, "XMLHttpRequestEvents.js");
        auto descriptor = Napi::Object::New(env);
        descriptor.Set("value", eventFactory);
        auto object = env.Global().Get("Object").As<Napi::Object>();
        object.Get("defineProperty").As<Napi::Function>().Call(object, {func, Napi::String::New(env, EVENT_FACTORY_NAME), descriptor});

        if (env.Global().Get(JS_XML_HTTP_REQUEST_CONSTRUCTOR_NAME).IsUndefined())
        {
            env.Global().Set(JS_XML_HTTP_REQUEST_CONSTRUCTOR_NAME, func);
        }

        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_XML_HTTP_REQUEST_CONSTRUCTOR_NAME, func);
    }

    XMLHttpRequest::XMLHttpRequest(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<XMLHttpRequest>{info}
        , m_runtimeScheduler{JsRuntime::GetFromJavaScript(info.Env())}
        , m_makeEvent{Napi::Persistent(info.NewTarget().As<Napi::Object>().Get(EVENT_FACTORY_NAME).As<Napi::Function>())}
    {
    }

    Napi::Value XMLHttpRequest::GetReadyState(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), arcana::underlying_cast(m_readyState));
    }

    Napi::Value XMLHttpRequest::GetResponse(const Napi::CallbackInfo&)
    {
        if (m_request->ResponseType() == UrlLib::UrlResponseType::String)
        {
            const std::string_view responseString{m_request->ResponseString()};
            return Napi::String::New(Env(), responseString.data(), responseString.size());
        }
        else
        {
            gsl::span<const std::byte> responseBuffer{m_request->ResponseBuffer()};
            auto arrayBuffer{Napi::ArrayBuffer::New(Env(), responseBuffer.size())};
            std::memcpy(arrayBuffer.Data(), responseBuffer.data(), arrayBuffer.ByteLength());
            return arrayBuffer;
        }
    }

    Napi::Value XMLHttpRequest::GetResponseText(const Napi::CallbackInfo&)
    {
        // The body may legitimately contain embedded nulls: Emscripten's EXPORT_ES6 output, for
        // example, inlines the .wasm payload as a JavaScript string literal. Passing .data()
        // alone would hand a const char* to Napi and truncate at the first null, so the length
        // has to be supplied explicitly.
        const std::string_view responseString{m_request->ResponseString()};
        return Napi::String::New(Env(), responseString.data(), responseString.size());
    }

    Napi::Value XMLHttpRequest::GetResponseType(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), ResponseType::EnumToString(m_request->ResponseType()));
    }

    void XMLHttpRequest::SetResponseType(const Napi::CallbackInfo&, const Napi::Value& value)
    {
        m_request->ResponseType(ResponseType::StringToEnum(value.As<Napi::String>().Utf8Value()));
    }

    Napi::Value XMLHttpRequest::GetResponseURL(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_request->ResponseUrl().data());
    }

    Napi::Value XMLHttpRequest::GetStatus(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_statusCode);
    }

    Napi::Value XMLHttpRequest::GetStatusText(const Napi::CallbackInfo&)
    {
        // Per the XHR spec, statusText is the empty string until a response is available
        // (status 0 means UNSENT/OPENED or a network error).
        return Napi::String::New(Env(), m_statusText);
    }

    Napi::Value XMLHttpRequest::GetErrorCode(const Napi::CallbackInfo&)
    {
        // Stable symbolic token for a transport failure (e.g. "CURLE_COULDNT_CONNECT",
        // "NSURLErrorTimedOut", "AppResourceNotFound"); empty when there was no transport failure.
        return Napi::String::New(Env(), std::string{m_request->ErrorSymbol()});
    }

    Napi::Value XMLHttpRequest::GetErrorDetail(const Napi::CallbackInfo&)
    {
        // Full normalized "<domain>:<symbol>(<code>): <detail>" string; empty when there was no
        // transport failure.
        return Napi::String::New(Env(), std::string{m_request->ErrorString()});
    }

    Napi::Value XMLHttpRequest::GetResponseHeader(const Napi::CallbackInfo& info)
    {
        const auto headerName = info[0].As<Napi::String>().Utf8Value();
        const auto header = m_request->GetResponseHeader(headerName);
        return header ? Napi::Value::From(Env(), header.value()) : info.Env().Null();
    }

    Napi::Value XMLHttpRequest::GetAllResponseHeaders(const Napi::CallbackInfo&)
    {
        auto responseHeaders = m_request->GetAllResponseHeaders();
        Napi::Object responseHeadersObject = Napi::Object::New(Env());

        for (auto& iter : responseHeaders)
        {
            auto key = Napi::String::New(Env(), iter.first);
            auto value = Napi::String::New(Env(), iter.second);
            responseHeadersObject.Set(key, value);
        }

        return responseHeadersObject;
    }

    void XMLHttpRequest::SetRequestHeader(const Napi::CallbackInfo& info)
    {
        m_request->SetRequestHeader(info[0].As<Napi::String>().Utf8Value(), info[1].As<Napi::String>().Utf8Value());
    }

    void XMLHttpRequest::AddEventListener(const Napi::CallbackInfo& info)
    {
        const std::string eventType = info[0].As<Napi::String>().Utf8Value();
        const Napi::Function eventHandler = info[1].As<Napi::Function>();

        auto& listeners = m_listeners[eventType];
        for (const auto& listener : listeners)
        {
            // Deliberately skips the `on<event>` entry: `xhr.onload = f` followed by
            // `xhr.addEventListener("load", f)` is two independent registrations, and a browser
            // calls `f` twice rather than collapsing them.
            if (listener->active && !listener->isEventHandler && listener->callback.Value() == eventHandler)
            {
                // Per DOM, re-adding an identical (type, callback, capture) triple is a silent
                // no-op rather than an error: "If eventTarget's event listener list does not
                // contain an event listener whose type is listener's type [...] then append
                // listener". The listener stays registered once and is dispatched once.
                return;
            }
        }

        listeners.push_back(std::make_shared<Listener>(Listener{Napi::Persistent(eventHandler.As<Napi::Object>()), false}));
    }

    void XMLHttpRequest::RemoveEventListener(const Napi::CallbackInfo& info)
    {
        const std::string eventType = info[0].As<Napi::String>().Utf8Value();
        const Napi::Function eventHandler = info[1].As<Napi::Function>();
        const auto itType = m_listeners.find(eventType);
        if (itType != m_listeners.end())
        {
            auto& listeners = itType->second;
            for (auto it = listeners.begin(); it != listeners.end(); ++it)
            {
                // removeEventListener never removes an `on<event>` handler; that is done by
                // assigning null to the property.
                if ((*it)->active && !(*it)->isEventHandler && (*it)->callback.Value() == eventHandler)
                {
                    (*it)->active = false;
                    listeners.erase(it);
                    break;
                }
            }
        }
    }

    void XMLHttpRequest::Abort(const Napi::CallbackInfo& info)
    {
        if (m_readyState == ReadyState::Done)
        {
            m_readyState = ReadyState::Unsent;
            return;
        }

        if (!m_sendActive)
        {
            return;
        }

        m_sendActive = false;
        const auto abortedSendId = ++m_sendId;
        m_statusCode = 0;
        m_statusText.clear();
        m_request->Abort();

        auto jsThis = info.This().As<Napi::Object>();
        SetReadyState(ReadyState::Done, jsThis);
        if (m_sendId != abortedSendId)
        {
            return;
        }
        RaiseEvent(EventType::Abort, jsThis);
        if (m_sendId != abortedSendId)
        {
            return;
        }
        RaiseEvent(EventType::LoadEnd, jsThis);
        if (m_sendId == abortedSendId)
        {
            m_readyState = ReadyState::Unsent;
        }
    }

    void XMLHttpRequest::Open(const Napi::CallbackInfo& info)
    {
        m_url = info[1].As<Napi::String>();

        try
        {
            if (m_sendActive)
            {
                m_request->Abort();
                m_sendActive = false;
            }
            ++m_sendId;
            m_request = std::make_shared<UrlLib::UrlRequest>();
            m_request->Open(MethodType::StringToEnum(info[0].As<Napi::String>().Utf8Value()), m_url);
            m_statusCode = 0;
            m_statusText.clear();
        }
        catch (const std::exception& e)
        {
            throw Napi::Error::New(info.Env(), std::string{"Error opening URL: "} + e.what());
        }
        catch (...)
        {
            throw Napi::Error::New(info.Env(), "Unknown error opening URL");
        }

        SetReadyState(ReadyState::Opened, info.This().As<Napi::Object>());
    }

    void XMLHttpRequest::Send(const Napi::CallbackInfo& info)
    {
        if (m_readyState != ReadyState::Opened)
        {
            throw Napi::Error::New(info.Env(), "XMLHttpRequest must be opened before it can be sent");
        }

        if (info.Length() > 0)
        {
            if (!info[0].IsString() && !info[0].IsUndefined() && !info[0].IsNull())
            {
                throw Napi::Error::New(info.Env(), "Only strings are supported in XMLHttpRequest body");
            }

            if (info[0].IsString())
            {
                m_request->SetRequestBody(info[0].As<Napi::String>().Utf8Value());
            }
        }

        std::string traceName = (std::ostringstream{} << "XMLHttpRequest::Send [" << m_url << "]").str();
        auto sendRegion = std::make_optional<arcana::trace_region>(traceName.c_str());

        // Keep the JS wrapper (and therefore this C++ object) alive for the
        // duration of the asynchronous request. The continuation below captures
        // `this` raw and dereferences members when the request settles; without
        // an anchor, GC may collect the wrapper while the request is in flight
        // (e.g. once the requesting script drops its reference) and the
        // continuation would then run on a freed `this`. The anchor lives in a
        // shared_ptr owned by the continuation lambda, so it is released
        // automatically once the request settles and the lambda is destroyed --
        // no member self-reference to clear. (Mirrors FileReader's anchor.)
        auto anchor = std::make_shared<Napi::ObjectReference>(Napi::Persistent(info.This().As<Napi::Object>()));
        const auto request = m_request;
        const auto sendId = ++m_sendId;
        m_sendActive = true;

        request->SendAsync()
            .then(arcana::inline_scheduler, arcana::cancellation::none(), [sendRegion{std::move(sendRegion)}]() mutable {
                sendRegion.reset();
            })
            .then(m_runtimeScheduler, arcana::cancellation::none(), [this, anchor{std::move(anchor)}, request, sendId](const arcana::expected<void, std::exception_ptr>& result) {
                if (sendId != m_sendId)
                {
                    return;
                }

                m_sendActive = false;
                // Run on every outcome -- transport exception OR underlying request succeeded but ended in a non-2xx
                // status (e.g. a missing local file on UWP, where UrlLib silently retains status 0). The previous
                // success-only continuation here skipped readyState=Done / loadend / error and let the JS observer
                // hang.
                const auto statusCode = arcana::underlying_cast(request->StatusCode());
                // `error` is reserved for transport-level failure. A completed HTTP transaction
                // that returned a non-2xx status (e.g. 404) is still a successful exchange, so it
                // dispatches `load` and the caller branches on `xhr.status` inside the handler.
                // UrlStatusCode::None (0) is UrlLib's "no response was obtained" sentinel: it is
                // only ever the initial value and the reset in ResetForOpen, because every path
                // that produces a response assigns an explicit code -- including the non-HTTP
                // ones, where local file reads set Ok. That keeps the missing-local-file-on-UWP
                // case (status left at 0) reporting `error`.
                const bool failed = result.has_error() || statusCode == 0;
                m_statusCode = statusCode;
                m_statusText = statusCode == 0 ? "" : std::string{request->StatusText()};

                auto jsThis = anchor->Value();
                SetReadyState(ReadyState::Done, jsThis);
                if (sendId != m_sendId)
                {
                    return;
                }
                if (failed)
                {
                    RaiseEvent(EventType::Error, jsThis);
                }
                else
                {
                    RaiseEvent(EventType::Load, jsThis);
                }
                if (sendId == m_sendId)
                {
                    RaiseEvent(EventType::LoadEnd, jsThis);
                }
            });
    }

    void XMLHttpRequest::SetReadyState(ReadyState readyState, const Napi::Object& jsThis)
    {
        m_readyState = readyState;
        RaiseEvent(EventType::ReadyStateChange, jsThis);
    }

    void XMLHttpRequest::RaiseEvent(const char* eventType, const Napi::Object& jsThis)
    {
        std::string traceName = (std::ostringstream{} << "XMLHttpRequest::RaiseEvent [" << eventType << "] [" << m_url << "]").str();
        arcana::trace_region raiseEventRegion{traceName.c_str()};

        Napi::Env env = Env();

        // Snapshot stable listener records before dispatching. A handler may call addEventListener,
        // removeEventListener, or reassign an on<event> property while it runs, which would
        // otherwise reallocate the vector or rehash the map out from under this dispatch.
        std::vector<std::shared_ptr<Listener>> listeners{};

        const auto it = m_listeners.find(eventType);
        if (it != m_listeners.end())
        {
            listeners = it->second;
        }

        auto dispatch = m_makeEvent.Value().Call({
            Napi::String::New(env, eventType),
            jsThis,
            Napi::Boolean::New(env, std::string_view{eventType} != EventType::ReadyStateChange),
        }).As<Napi::Object>();
        auto event = dispatch.Get("value").As<Napi::Object>();

        std::vector<std::shared_ptr<Napi::Error>> unhandledErrors{};
        for (const auto& listener : listeners)
        {
            if (!listener->active)
            {
                continue;
            }

            const auto callback = listener->callback.Value();
            if (!callback.IsFunction())
            {
                continue;
            }

            try
            {
                callback.As<Napi::Function>().Call(jsThis, {event});
            }
            catch (const Napi::Error& error)
            {
                unhandledErrors.push_back(std::make_shared<Napi::Error>(error));
                continue;
            }

            if (env.IsExceptionPending())
            {
                auto error = env.GetAndClearPendingException();
                unhandledErrors.push_back(std::make_shared<Napi::Error>(std::move(error)));
            }
            if (dispatch.Get("isStopped").As<Napi::Function>().Call(dispatch, {}).ToBoolean().Value())
            {
                break;
            }
        }

        dispatch.Get("end").As<Napi::Function>().Call(dispatch, {});
        for (const auto& error : unhandledErrors)
        {
            m_runtimeScheduler([error]() {
                error->ThrowAsJavaScriptException();
            });
        }
    }
}

namespace Babylon::Polyfills::XMLHttpRequest
{
    void BABYLON_API Initialize(Napi::Env env)
    {
        Internal::XMLHttpRequest::Initialize(env);
    }
}
