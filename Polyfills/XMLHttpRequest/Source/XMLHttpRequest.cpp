#include "XMLHttpRequest.h"
#include <Babylon/JsRuntime.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Polyfills/URL.h>
#include <arcana/tracing/trace_region.h>
#include <cctype>
#include <cstring>
#include <sstream>

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
        }

        constexpr const char* BlobUrlScheme = "blob:";

        bool EqualsIgnoreCase(const std::string& a, const char* b)
        {
            size_t i = 0;
            for (; i < a.size() && b[i] != '\0'; ++i)
            {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                {
                    return false;
                }
            }
            return i == a.size() && b[i] == '\0';
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
                InstanceMethod("getAllResponseHeaders", &XMLHttpRequest::GetAllResponseHeaders),
                InstanceMethod("getResponseHeader", &XMLHttpRequest::GetResponseHeader),
                InstanceMethod("setRequestHeader", &XMLHttpRequest::SetRequestHeader),
                InstanceMethod("addEventListener", &XMLHttpRequest::AddEventListener),
                InstanceMethod("removeEventListener", &XMLHttpRequest::RemoveEventListener),
                InstanceMethod("abort", &XMLHttpRequest::Abort),
                InstanceMethod("open", &XMLHttpRequest::Open),
                InstanceMethod("send", &XMLHttpRequest::Send),
            });

        if (env.Global().Get(JS_XML_HTTP_REQUEST_CONSTRUCTOR_NAME).IsUndefined())
        {
            env.Global().Set(JS_XML_HTTP_REQUEST_CONSTRUCTOR_NAME, func);
        }

        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_XML_HTTP_REQUEST_CONSTRUCTOR_NAME, func);
    }

    XMLHttpRequest::XMLHttpRequest(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<XMLHttpRequest>{info}
        , m_runtimeScheduler{JsRuntime::GetFromJavaScript(info.Env())}
    {
    }

    Napi::Value XMLHttpRequest::GetReadyState(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), arcana::underlying_cast(m_readyState));
    }

    Napi::Value XMLHttpRequest::GetResponse(const Napi::CallbackInfo&)
    {
        if (m_isBlobRequest)
        {
            if (m_request.ResponseType() == UrlLib::UrlResponseType::String)
            {
                return Napi::String::New(Env(), reinterpret_cast<const char*>(m_blobData.data()), m_blobData.size());
            }

            auto arrayBuffer{Napi::ArrayBuffer::New(Env(), m_blobData.size())};
            if (!m_blobData.empty())
            {
                std::memcpy(arrayBuffer.Data(), m_blobData.data(), m_blobData.size());
            }
            return arrayBuffer;
        }

        if (m_request.ResponseType() == UrlLib::UrlResponseType::String)
        {
            return Napi::Value::From(Env(), m_request.ResponseString().data());
        }
        else
        {
            gsl::span<const std::byte> responseBuffer{m_request.ResponseBuffer()};
            auto arrayBuffer{Napi::ArrayBuffer::New(Env(), responseBuffer.size())};
            std::memcpy(arrayBuffer.Data(), responseBuffer.data(), arrayBuffer.ByteLength());
            return arrayBuffer;
        }
    }

    Napi::Value XMLHttpRequest::GetResponseText(const Napi::CallbackInfo&)
    {
        if (m_isBlobRequest)
        {
            return Napi::String::New(Env(), reinterpret_cast<const char*>(m_blobData.data()), m_blobData.size());
        }

        return Napi::Value::From(Env(), m_request.ResponseString().data());
    }

    Napi::Value XMLHttpRequest::GetResponseType(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), ResponseType::EnumToString(m_request.ResponseType()));
    }

    void XMLHttpRequest::SetResponseType(const Napi::CallbackInfo&, const Napi::Value& value)
    {
        m_request.ResponseType(ResponseType::StringToEnum(value.As<Napi::String>().Utf8Value()));
    }

    Napi::Value XMLHttpRequest::GetResponseURL(const Napi::CallbackInfo&)
    {
        if (m_isBlobRequest)
        {
            return Napi::String::New(Env(), m_url);
        }

        return Napi::Value::From(Env(), m_request.ResponseUrl().data());
    }

    Napi::Value XMLHttpRequest::GetStatus(const Napi::CallbackInfo&)
    {
        if (m_isBlobRequest)
        {
            return Napi::Value::From(Env(), m_blobResolved ? 200 : 0);
        }

        return Napi::Value::From(Env(), arcana::underlying_cast(m_request.StatusCode()));
    }

    Napi::Value XMLHttpRequest::GetStatusText(const Napi::CallbackInfo&)
    {
        if (m_isBlobRequest)
        {
            return Napi::String::New(Env(), m_blobResolved ? "OK" : "");
        }

        // Per the XHR spec, statusText is the empty string until a response is available
        // (status 0 means UNSENT/OPENED or a network error).
        if (arcana::underlying_cast(m_request.StatusCode()) == 0)
        {
            return Napi::String::New(Env(), "");
        }

        return Napi::String::New(Env(), std::string{m_request.StatusText()});
    }

    Napi::Value XMLHttpRequest::GetErrorCode(const Napi::CallbackInfo&)
    {
        // Stable symbolic token for a transport failure (e.g. "CURLE_COULDNT_CONNECT",
        // "NSURLErrorTimedOut", "AppResourceNotFound"); empty when there was no transport failure.
        return Napi::String::New(Env(), std::string{m_request.ErrorSymbol()});
    }

    Napi::Value XMLHttpRequest::GetErrorDetail(const Napi::CallbackInfo&)
    {
        // Full normalized "<domain>:<symbol>(<code>): <detail>" string; empty when there was no
        // transport failure.
        return Napi::String::New(Env(), std::string{m_request.ErrorString()});
    }

    Napi::Value XMLHttpRequest::GetResponseHeader(const Napi::CallbackInfo& info)
    {
        const auto headerName = info[0].As<Napi::String>().Utf8Value();

        if (m_isBlobRequest)
        {
            if (m_blobResolved && EqualsIgnoreCase(headerName, "content-type"))
            {
                return Napi::String::New(Env(), m_blobType);
            }
            return info.Env().Null();
        }

        const auto header = m_request.GetResponseHeader(headerName);
        return header ? Napi::Value::From(Env(), header.value()) : info.Env().Null();
    }

    Napi::Value XMLHttpRequest::GetAllResponseHeaders(const Napi::CallbackInfo&)
    {
        Napi::Object responseHeadersObject = Napi::Object::New(Env());

        if (m_isBlobRequest)
        {
            if (m_blobResolved)
            {
                responseHeadersObject.Set(Napi::String::New(Env(), "content-type"), Napi::String::New(Env(), m_blobType));
            }
            return responseHeadersObject;
        }

        auto responseHeaders = m_request.GetAllResponseHeaders();

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
        m_request.SetRequestHeader(info[0].As<Napi::String>().Utf8Value(), info[1].As<Napi::String>().Utf8Value());
    }

    void XMLHttpRequest::AddEventListener(const Napi::CallbackInfo& info)
    {
        const std::string eventType = info[0].As<Napi::String>().Utf8Value();
        const Napi::Function eventHandler = info[1].As<Napi::Function>();

        const auto& eventHandlerRefs = m_eventHandlerRefs[eventType];
        for (auto it = eventHandlerRefs.begin(); it != eventHandlerRefs.end(); ++it)
        {
            if (it->Value() == eventHandler)
            {
                throw Napi::Error::New(info.Env(), "Cannot add the same event handler twice");
            }
        }

        m_eventHandlerRefs[eventType].push_back(Napi::Persistent(eventHandler));
    }

    void XMLHttpRequest::RemoveEventListener(const Napi::CallbackInfo& info)
    {
        const std::string eventType = info[0].As<Napi::String>().Utf8Value();
        const Napi::Function eventHandler = info[1].As<Napi::Function>();
        const auto itType = m_eventHandlerRefs.find(eventType);
        if (itType != m_eventHandlerRefs.end())
        {
            auto& eventHandlerRefs = itType->second;
            for (auto it = eventHandlerRefs.begin(); it != eventHandlerRefs.end(); ++it)
            {
                if (it->Value() == eventHandler)
                {
                    eventHandlerRefs.erase(it);
                    break;
                }
            }
        }
    }

    void XMLHttpRequest::Abort(const Napi::CallbackInfo&)
    {
        m_request.Abort();
    }

    void XMLHttpRequest::Open(const Napi::CallbackInfo& info)
    {
        // Clear any state left over from a previous use of this instance so that reusing a single
        // XMLHttpRequest across multiple requests never mixes blob: and transport request state
        // (e.g. a later non-blob request being mistaken for a blob request).
        m_isBlobRequest = false;
        m_blobResolved = false;
        m_blobData.clear();
        m_blobType.clear();

        m_url = info[1].As<Napi::String>();

        try
        {
            // Validate the HTTP method for every request, including blob: URLs, so unsupported
            // verbs are rejected consistently regardless of the URL scheme.
            const auto method = MethodType::StringToEnum(info[0].As<Napi::String>().Utf8Value());

            // blob: URLs (URL.createObjectURL) are served from the in-memory object-URL store
            // rather than the UrlLib transport, which only understands app/file/http(s). The store
            // is (re-)resolved at Send() time so revoke-after-open is honored.
            if (m_url.rfind(BlobUrlScheme, 0) == 0)
            {
                m_isBlobRequest = true;
            }
            else
            {
                m_request.Open(method, m_url);
            }
        }
        catch (const std::exception& e)
        {
            throw Napi::Error::New(info.Env(), std::string{"Error opening URL: "} + e.what());
        }
        catch (...)
        {
            throw Napi::Error::New(info.Env(), "Unknown error opening URL");
        }

        SetReadyState(ReadyState::Opened);
    }

    void XMLHttpRequest::Send(const Napi::CallbackInfo& info)
    {
        if (m_readyState != ReadyState::Opened)
        {
            throw Napi::Error::New(info.Env(), "XMLHttpRequest must be opened before it can be sent");
        }

        // blob: request: resolve the object-URL store now (at send() time, not open()) so a blob:
        // URL revoked between open() and send() is reported as a network error (status 0 + error),
        // matching browser revoke semantics. Deliver the completion events asynchronously (mirroring
        // a real transport) so listeners registered after send() still fire.
        if (m_isBlobRequest)
        {
            m_blobData.clear();
            m_blobType.clear();
            m_blobResolved = Babylon::Polyfills::URL::TryResolveObjectURL(info.Env(), m_url, m_blobData, m_blobType);

            auto anchor = std::make_shared<Napi::ObjectReference>(Napi::Persistent(info.This().As<Napi::Object>()));

            m_runtimeScheduler([this, anchor{std::move(anchor)}]() {
                SetReadyState(ReadyState::Done);
                if (!m_blobResolved)
                {
                    RaiseEvent(EventType::Error);
                }
                RaiseEvent(EventType::LoadEnd);
                m_eventHandlerRefs.clear();
            });

            return;
        }

        if (info.Length() > 0)
        {
            if (!info[0].IsString() && !info[0].IsUndefined() && !info[0].IsNull())
            {
                throw Napi::Error::New(info.Env(), "Only strings are supported in XMLHttpRequest body");
            }

            if (info[0].IsString())
            {
                m_request.SetRequestBody(info[0].As<Napi::String>().Utf8Value());
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

        m_request.SendAsync()
            .then(arcana::inline_scheduler, arcana::cancellation::none(), [sendRegion{std::move(sendRegion)}]() mutable {
                sendRegion.reset();
            })
            .then(m_runtimeScheduler, arcana::cancellation::none(), [this, anchor{std::move(anchor)}](const arcana::expected<void, std::exception_ptr>& result) {
                // Run on every outcome -- transport exception OR underlying request succeeded but ended in a non-2xx
                // status (e.g. a missing local file on UWP, where UrlLib silently retains status 0). The previous
                // success-only continuation here skipped readyState=Done / loadend / error and let the JS observer
                // hang.
                const auto statusCode = arcana::underlying_cast(m_request.StatusCode());
                const bool failed = result.has_error() || statusCode < 200 || statusCode >= 300;

                SetReadyState(ReadyState::Done);
                if (failed)
                {
                    RaiseEvent(EventType::Error);
                }
                RaiseEvent(EventType::LoadEnd);

                // Assume the XMLHttpRequest will only be used for a single request and clear the event handlers.
                // Single use seems to be the standard pattern, and we need to release our strong refs to event handlers.
                m_eventHandlerRefs.clear();
            });
    }

    void XMLHttpRequest::SetReadyState(ReadyState readyState)
    {
        m_readyState = readyState;
        RaiseEvent(EventType::ReadyStateChange);
    }

    void XMLHttpRequest::RaiseEvent(const char* eventType)
    {
        std::string traceName = (std::ostringstream{} << "XMLHttpRequest::RaiseEvent [" << eventType << "] [" << m_url << "]").str();
        arcana::trace_region raiseEventRegion{traceName.c_str()};
        const auto it = m_eventHandlerRefs.find(eventType);
        if (it != m_eventHandlerRefs.end())
        {
            const auto& eventHandlerRefs = it->second;
            for (const auto& eventHandlerRef : eventHandlerRefs)
            {
                eventHandlerRef.Call({});
            }
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
