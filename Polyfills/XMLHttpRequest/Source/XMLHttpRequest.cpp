#include "XMLHttpRequest.h"
#include <Babylon/JsRuntime.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <arcana/tracing/trace_region.h>
#include <gsl/gsl>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
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
        }

        bool TryOpenAsGet(UrlLib::UrlRequest& request, const std::string& url)
        {
            try
            {
                request.Open(UrlLib::UrlMethod::Get, url);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        namespace UrlSyntax
        {
            bool IsAlpha(char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            }

            bool IsDigit(char c)
            {
                return c >= '0' && c <= '9';
            }

            bool IsHexDigit(char c)
            {
                return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            }

            bool IsSlash(char c, bool special)
            {
                return c == '/' || (special && c == '\\');
            }

            bool IsSpecialScheme(const std::string& scheme)
            {
                return scheme == "http" || scheme == "https" || scheme == "ws" || scheme == "wss" || scheme == "ftp";
            }

            // The authority ends at the first '/', '?' or '#' ('\\' also ends it for special schemes).
            std::string_view ExtractAuthority(std::string_view value, bool special)
            {
                size_t end = 0;
                while (end < value.size() && !IsSlash(value[end], special) && value[end] != '?' && value[end] != '#')
                {
                    ++end;
                }
                return value.substr(0, end);
            }

            bool IsHostValid(std::string_view host)
            {
                for (size_t i = 0; i < host.size(); ++i)
                {
                    const unsigned char c = static_cast<unsigned char>(host[i]);
                    if (c <= 0x20 || c == 0x7F || std::strchr("#/<>?@[\\]^|", c) != nullptr)
                    {
                        return false;
                    }
                    if (c == '%' && (i + 2 >= host.size() || !IsHexDigit(host[i + 1]) || !IsHexDigit(host[i + 2])))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool IsPortValid(std::string_view port)
            {
                if (port.size() > 5)
                {
                    return false;
                }

                uint32_t value = 0;
                for (const char c : port)
                {
                    if (!IsDigit(c))
                    {
                        return false;
                    }
                    value = value * 10 + static_cast<uint32_t>(c - '0');
                }
                return value <= 65535;
            }

            bool IsAuthorityValid(std::string_view authority, bool special)
            {
                const auto userInfoEnd = authority.rfind('@');
                if (userInfoEnd != std::string_view::npos)
                {
                    authority.remove_prefix(userInfoEnd + 1);
                    if (authority.empty())
                    {
                        return false;
                    }
                }

                std::string_view host{authority};
                std::string_view port{};
                if (!authority.empty() && authority.front() == '[')
                {
                    const auto close = authority.find(']');
                    if (close == std::string_view::npos)
                    {
                        return false;
                    }

                    host = authority.substr(1, close - 1);
                    const auto rest = authority.substr(close + 1);
                    if (!rest.empty())
                    {
                        if (rest.front() != ':')
                        {
                            return false;
                        }
                        port = rest.substr(1);
                    }

                    if (host.empty() || host.find_first_not_of("0123456789abcdefABCDEF:.") != std::string_view::npos)
                    {
                        return false;
                    }
                }
                else
                {
                    const auto colon = authority.find(':');
                    if (colon != std::string_view::npos)
                    {
                        host = authority.substr(0, colon);
                        port = authority.substr(colon + 1);
                    }

                    if (!IsHostValid(host))
                    {
                        return false;
                    }
                }

                return IsPortValid(port) && (!special || !host.empty());
            }

            // Approximates whether the WHATWG URL parser accepts the string when resolved against a
            // placeholder special-scheme base (so scheme-less relative references like "model.glb" parse,
            // while "//" or "http://" do not). It only classifies the input -- the string passed to the
            // transport is never rewritten -- so no document-relative URL resolution is introduced.
            bool IsParseable(const std::string& input)
            {
                size_t first = 0;
                size_t last = input.size();
                while (first < last && static_cast<unsigned char>(input[first]) <= 0x20)
                {
                    ++first;
                }
                while (last > first && static_cast<unsigned char>(input[last - 1]) <= 0x20)
                {
                    --last;
                }

                std::string url;
                for (size_t i = first; i < last; ++i)
                {
                    if (input[i] != '\t' && input[i] != '\n' && input[i] != '\r')
                    {
                        url.push_back(input[i]);
                    }
                }

                std::string_view rest{url};
                size_t schemeEnd = 0;
                if (!url.empty() && IsAlpha(url[0]))
                {
                    schemeEnd = 1;
                    while (schemeEnd < url.size() && (IsAlpha(url[schemeEnd]) || IsDigit(url[schemeEnd]) || url[schemeEnd] == '+' || url[schemeEnd] == '-' || url[schemeEnd] == '.'))
                    {
                        ++schemeEnd;
                    }
                }

                if (schemeEnd > 0 && schemeEnd < url.size() && url[schemeEnd] == ':')
                {
                    std::string scheme{url, 0, schemeEnd};
                    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    rest.remove_prefix(schemeEnd + 1);

                    if (scheme == "file")
                    {
                        return true;
                    }

                    if (IsSpecialScheme(scheme))
                    {
                        while (!rest.empty() && IsSlash(rest.front(), true))
                        {
                            rest.remove_prefix(1);
                        }
                        return IsAuthorityValid(ExtractAuthority(rest, true), true);
                    }

                    if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/')
                    {
                        return IsAuthorityValid(ExtractAuthority(rest.substr(2), false), false);
                    }
                    return true;
                }

                if (rest.size() >= 2 && IsSlash(rest[0], true) && IsSlash(rest[1], true))
                {
                    while (!rest.empty() && IsSlash(rest.front(), true))
                    {
                        rest.remove_prefix(1);
                    }
                    return IsAuthorityValid(ExtractAuthority(rest, true), true);
                }

                return true;
            }
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
        if (m_request.ResponseType() == UrlLib::UrlResponseType::String)
        {
            const std::string_view responseString{m_request.ResponseString()};
            return Napi::String::New(Env(), responseString.data(), responseString.size());
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
        // The body may legitimately contain embedded nulls: Emscripten's EXPORT_ES6 output, for
        // example, inlines the .wasm payload as a JavaScript string literal. Passing .data()
        // alone would hand a const char* to Napi and truncate at the first null, so the length
        // has to be supplied explicitly.
        const std::string_view responseString{m_request.ResponseString()};
        return Napi::String::New(Env(), responseString.data(), responseString.size());
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
        return Napi::Value::From(Env(), m_request.ResponseUrl().data());
    }

    Napi::Value XMLHttpRequest::GetStatus(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), arcana::underlying_cast(m_request.StatusCode()));
    }

    Napi::Value XMLHttpRequest::GetStatusText(const Napi::CallbackInfo&)
    {
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
        return Napi::String::New(Env(), m_openError ? "UrlOpenFailed" : std::string{m_request.ErrorSymbol()});
    }

    Napi::Value XMLHttpRequest::GetErrorDetail(const Napi::CallbackInfo&)
    {
        // Original opening error for UrlOpenFailed, or normalized
        // "<domain>:<symbol>(<code>): <detail>" for a send failure; empty on success.
        return Napi::String::New(Env(), m_openError.value_or(std::string{m_request.ErrorString()}));
    }

    Napi::Value XMLHttpRequest::GetResponseHeader(const Napi::CallbackInfo& info)
    {
        const auto headerName = info[0].As<Napi::String>().Utf8Value();
        const auto header = m_request.GetResponseHeader(headerName);
        return header ? Napi::Value::From(Env(), header.value()) : info.Env().Null();
    }

    Napi::Value XMLHttpRequest::GetAllResponseHeaders(const Napi::CallbackInfo&)
    {
        auto responseHeaders = m_request.GetAllResponseHeaders();
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
        ++m_requestGeneration;
        m_request.Abort();
        if (m_openErrorSent && m_readyState == ReadyState::Opened)
        {
            m_readyState = ReadyState::Unsent;
        }
    }

    void XMLHttpRequest::Open(const Napi::CallbackInfo& info)
    {
        UrlLib::UrlMethod method;
        const auto methodName = info[0].As<Napi::String>().Utf8Value();
        try
        {
            method = MethodType::StringToEnum(methodName);
        }
        catch (const std::exception& e)
        {
            throw Napi::Error::New(info.Env(), e.what());
        }

        // Per the XHR spec, a URL that cannot be parsed fails synchronously with SyntaxError; only
        // requests the transport later fails to open or fetch are reported through error events.
        const auto url = info[1].As<Napi::String>().Utf8Value();
        if (!UrlSyntax::IsParseable(url))
        {
            const auto syntaxErrorCtor = info.Env().Global().Get("SyntaxError").As<Napi::Function>();
            throw Napi::Error{info.Env(), syntaxErrorCtor.Call({Napi::String::New(info.Env(), "Failed to parse URL '" + url + "'")})};
        }

        m_url = url;
        m_openGeneration = ++m_requestGeneration;
        m_openError.reset();
        m_openErrorSent = false;

        try
        {
            m_request.Open(method, m_url);
        }
        catch (const std::exception& e)
        {
            // Some transports reject a method before looking at the URL (e.g. POST on Unix). If a GET
            // for the same URL opens, the method was the cause, which is an unsupported operation
            // rather than a URL failure, so it stays synchronous.
            if (method != UrlLib::UrlMethod::Get && TryOpenAsGet(m_request, m_url))
            {
                throw Napi::Error::New(info.Env(), "Unsupported url method for this platform: " + methodName + " (" + e.what() + ")");
            }

            m_openError = std::string{"Error opening URL: "} + e.what();
        }
        catch (...)
        {
            m_openError = "Unknown error opening URL";
        }

        SetReadyState(ReadyState::Opened);
    }

    void XMLHttpRequest::Send(const Napi::CallbackInfo& info)
    {
        if (m_readyState != ReadyState::Opened)
        {
            throw Napi::Error::New(info.Env(), "XMLHttpRequest must be opened before it can be sent");
        }

        if (m_openErrorSent)
        {
            throw Napi::Error::New(info.Env(), "XMLHttpRequest has already been sent");
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

        if (m_openError)
        {
            m_openErrorSent = true;
            auto anchor = std::make_shared<Napi::ObjectReference>(Napi::Persistent(info.This().As<Napi::Object>()));
            arcana::make_task(m_runtimeScheduler, arcana::cancellation::none(),
                [this, anchor{std::move(anchor)}, generation{m_requestGeneration}, openGeneration{m_openGeneration}]() {
                    // Release after dispatch unwinds, without clearing a reopened request's listeners.
                    const auto releaseListeners = gsl::finally([this, openGeneration]() {
                        if (m_openGeneration == openGeneration)
                        {
                            m_eventHandlerRefs.clear();
                        }
                    });
                    if (generation != m_requestGeneration)
                    {
                        return;
                    }

                    // Match an asynchronous transport failure, unless a callback reopens or aborts the request.
                    m_readyState = ReadyState::Done;
                    for (const auto event : {EventType::ReadyStateChange, EventType::Error, EventType::LoadEnd})
                    {
                        if (generation != m_requestGeneration)
                        {
                            return;
                        }
                        RaiseEvent(event);
                    }
                });
            return;
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
