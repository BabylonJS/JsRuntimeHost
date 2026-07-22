#include "Fetch.h"
#include "FetchScripts.h"

#include <Babylon/JsRuntime.h>
#include <Babylon/JsRuntimeScheduler.h>
#include <Babylon/Polyfills/Fetch.h>

#include <UrlLib/UrlLib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Babylon::Polyfills::Internal
{
    namespace
    {
        // Shared state for honoring an AbortSignal passed via init.signal. Co-owned by the "abort"
        // listener (which sets the flag, captures the reason, and cancels the transport) and the
        // completion continuation (which reports the AbortError and tears the listener down).
        struct AbortState
        {
            bool aborted{false};
            Napi::Reference<Napi::Value> reason;
            Napi::ObjectReference signal;
            Napi::FunctionReference listener;
        };

        // The reason a fetch was aborted: the signal's `reason` (per the modern AbortSignal), or a
        // fresh AbortError if the signal does not expose one.
        Napi::Value GetAbortReason(Napi::Env env, const Napi::Object& signal)
        {
            const Napi::Value reason = signal.Get("reason");
            if (!reason.IsUndefined() && !reason.IsNull())
            {
                return reason;
            }

            Napi::Error error = Napi::Error::New(env, "The operation was aborted.");
            error.Set("name", Napi::String::New(env, "AbortError"));
            return error.Value();
        }

        bool EqualsIgnoreCase(std::string_view a, std::string_view b)
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char l, unsigned char r) {
                return std::tolower(l) == std::tolower(r);
            });
        }

        struct DataUrlResponse
        {
            std::string contentType;
            std::string url;
            std::vector<uint8_t> body;
        };

        bool IsAsciiWhitespace(uint8_t value)
        {
            return value == 0x09 || value == 0x0A || value == 0x0C || value == 0x0D || value == 0x20;
        }

        void TrimAsciiWhitespace(std::string& value)
        {
            const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
                return IsAsciiWhitespace(character);
            });
            const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
                return IsAsciiWhitespace(character);
            }).base();
            value = first < last ? std::string{first, last} : std::string{};
        }

        int HexDigitValue(char value)
        {
            if (value >= '0' && value <= '9')
            {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f')
            {
                return value - 'a' + 10;
            }
            if (value >= 'A' && value <= 'F')
            {
                return value - 'A' + 10;
            }
            return -1;
        }

        std::vector<uint8_t> PercentDecode(std::string_view value)
        {
            std::vector<uint8_t> decoded;
            decoded.reserve(value.size());
            for (size_t index = 0; index < value.size(); ++index)
            {
                if (value[index] == '%' && index + 2 < value.size())
                {
                    const int high = HexDigitValue(value[index + 1]);
                    const int low = HexDigitValue(value[index + 2]);
                    if (high >= 0 && low >= 0)
                    {
                        decoded.push_back(static_cast<uint8_t>((high << 4) | low));
                        index += 2;
                        continue;
                    }
                }
                decoded.push_back(static_cast<uint8_t>(value[index]));
            }
            return decoded;
        }

        int Base64DigitValue(uint8_t value)
        {
            if (value >= 'A' && value <= 'Z')
            {
                return value - 'A';
            }
            if (value >= 'a' && value <= 'z')
            {
                return value - 'a' + 26;
            }
            if (value >= '0' && value <= '9')
            {
                return value - '0' + 52;
            }
            if (value == '+')
            {
                return 62;
            }
            if (value == '/')
            {
                return 63;
            }
            return -1;
        }

        std::vector<uint8_t> ForgivingBase64Decode(const std::vector<uint8_t>& input)
        {
            size_t digitCount{};
            size_t paddingCount{};
            bool sawPadding{};
            for (const auto value : input)
            {
                if (IsAsciiWhitespace(value))
                {
                    continue;
                }
                if (value == '=')
                {
                    sawPadding = true;
                    ++paddingCount;
                    continue;
                }
                if (sawPadding || Base64DigitValue(value) < 0)
                {
                    throw std::runtime_error{"fetch: invalid base64 data URL"};
                }
                ++digitCount;
            }

            const auto encodedCount = digitCount + paddingCount;
            if ((paddingCount > 0 && (encodedCount % 4 != 0 || paddingCount > 2)) || digitCount % 4 == 1)
            {
                throw std::runtime_error{"fetch: invalid base64 data URL"};
            }

            std::vector<uint8_t> decoded;
            decoded.reserve(digitCount * 6 / 8);
            uint32_t accumulator{};
            size_t availableBits{};
            for (const auto value : input)
            {
                if (IsAsciiWhitespace(value) || value == '=')
                {
                    continue;
                }
                accumulator = (accumulator << 6) | static_cast<uint32_t>(Base64DigitValue(value));
                availableBits += 6;
                if (availableBits >= 8)
                {
                    availableBits -= 8;
                    decoded.push_back(static_cast<uint8_t>(accumulator >> availableBits));
                    accumulator &= (uint32_t{1} << availableBits) - 1;
                }
            }
            return decoded;
        }

        std::optional<DataUrlResponse> ParseDataUrl(std::string_view url)
        {
            if (url.size() < 5 || !EqualsIgnoreCase(url.substr(0, 4), "data") || url[4] != ':')
            {
                return std::nullopt;
            }

            const auto comma = url.find(',', 5);
            if (comma == std::string_view::npos)
            {
                throw std::runtime_error{"fetch: malformed data URL"};
            }

            std::string mediaType{url.substr(5, comma - 5)};
            TrimAsciiWhitespace(mediaType);
            bool base64 = false;
            if (const auto semicolon = mediaType.rfind(';'); semicolon != std::string::npos)
            {
                std::string finalParameter{mediaType.substr(semicolon + 1)};
                TrimAsciiWhitespace(finalParameter);
                if (EqualsIgnoreCase(finalParameter, "base64"))
                {
                    mediaType.resize(semicolon);
                    TrimAsciiWhitespace(mediaType);
                    base64 = true;
                }
            }
            if (mediaType.empty())
            {
                mediaType = "text/plain;charset=US-ASCII";
            }
            else if (mediaType.front() == ';')
            {
                mediaType.insert(0, "text/plain");
            }

            const auto fragment = url.find('#', comma + 1);
            const auto payload = url.substr(comma + 1, fragment == std::string_view::npos ? std::string_view::npos : fragment - comma - 1);
            auto decodedPayload = PercentDecode(payload);
            if (base64)
            {
                decodedPayload = ForgivingBase64Decode(decodedPayload);
            }

            return DataUrlResponse{
                std::move(mediaType),
                std::string{url.substr(0, fragment)},
                std::move(decodedPayload)};
        }

        // Stable message used for every transport-failure rejection. Browsers and Node both keep
        // this constant (the variable detail rides on `cause`) so crash-report grouping stays
        // intact; we follow Node/undici's "fetch failed" spelling.
        constexpr const char* FETCH_FAILED_MESSAGE = "fetch failed";

        // Snapshot the JS call-site stack synchronously, inside fetch(), before SendAsync() hands
        // the request to a worker thread. The transport-failure rejection is otherwise built in a
        // continuation that runs after fetch() has returned, where an Error would capture zero user
        // frames. We go through the global JS `Error` constructor (rather than napi_create_error) so
        // engines that materialize `.stack` from the JS constructor path capture the live caller
        // frames. The result is a plain std::string, safe to carry across the thread hop (unlike a
        // Napi::Reference, which must be created and destroyed on the JS thread). Empty if the engine
        // does not expose a stack at construction time (e.g. Chakra, which only populates `.stack`
        // when an error is thrown) -- in that case the rejection simply carries no synthetic frames.
        std::string CaptureCallSiteStack(Napi::Env env)
        {
            // Detect the global Error constructor with IsUndefined()/IsNull() rather than
            // IsFunction(): some JavaScriptCore/JSI builds classify constructor functions as
            // typeof 'object', so napi_typeof reports napi_object and IsFunction() would
            // incorrectly skip stack capture even though Error is callable (see the Blob check
            // below for the same rationale). Error is always present, so this guard is defensive.
            const Napi::Value errorCtor = env.Global().Get("Error");
            if (errorCtor.IsUndefined() || errorCtor.IsNull())
            {
                return {};
            }
            const Napi::Object error = errorCtor.As<Napi::Function>().New({});
            const Napi::Value stack = error.Get("stack");
            return stack.IsString() ? stack.As<Napi::String>().Utf8Value() : std::string{};
        }

        // Reattach the synchronously-captured frames to the rejection's Error, replacing the captured
        // header line (e.g. "Error\n    at ...") with one matching the TypeError we actually reject
        // with, so the stack reads correctly while preserving the user's call site.
        std::string ComposeRejectionStack(const std::string& capturedStack)
        {
            std::string header{"TypeError: "};
            header += FETCH_FAILED_MESSAGE;

            const auto firstNewline = capturedStack.find('\n');
            if (firstNewline == std::string::npos)
            {
                return header;
            }
            return header + capturedStack.substr(firstNewline);
        }

        // Build the transport-failure rejection: a TypeError with a stable message, carrying the
        // variable detail under `cause` (Node/undici shape) rather than as top-level own-properties.
        // `code`/`detail` come from UrlLib's normalized accessors and may be empty on backends that
        // do not yet populate them (Windows/Android today) -- in that case the standard observable
        // shape (TypeError + stable message + url) is preserved and only the extra detail is absent.
        Napi::Error BuildTransportError(Napi::Env env, const UrlLib::UrlRequest& request, const std::string& url, const std::string& capturedStack)
        {
            Napi::Error error = Napi::TypeError::New(env, FETCH_FAILED_MESSAGE);

            Napi::Object cause = Napi::Object::New(env);
            const std::string code{request.ErrorSymbol()};
            const std::string detail{request.ErrorString()};
            if (!code.empty())
            {
                cause.Set("code", Napi::String::New(env, code));
            }
            if (!detail.empty())
            {
                cause.Set("detail", Napi::String::New(env, detail));
            }
            cause.Set("url", Napi::String::New(env, url));
            cause.Set("status", Napi::Number::New(env, static_cast<double>(static_cast<int>(request.StatusCode()))));
            error.Set("cause", cause);

            if (!capturedStack.empty())
            {
                error.Set("stack", Napi::String::New(env, ComposeRejectionStack(capturedStack)));
            }

            return error;
        }

        // fetch only resolves for GET and POST because the underlying UrlLib transport supports nothing else.
        UrlLib::UrlMethod ParseMethod(const std::string& method)
        {
            if (EqualsIgnoreCase(method, "GET"))
            {
                return UrlLib::UrlMethod::Get;
            }
            if (EqualsIgnoreCase(method, "POST"))
            {
                return UrlLib::UrlMethod::Post;
            }

            throw std::runtime_error{"Unsupported fetch method: " + method + " (only GET and POST are supported)"};
        }

        void ApplyRequestHeaders(UrlLib::UrlRequest& request, const Napi::Value& headers)
        {
            if (headers.IsUndefined() || headers.IsNull())
            {
                return;
            }

            const auto env = headers.Env();
            const auto headersConstructor = env.Global().Get("Headers");
            if (headersConstructor.IsUndefined() || headersConstructor.IsNull())
            {
                throw Napi::TypeError::New(env, "fetch requires Headers to be installed.");
            }

            auto normalizedHeaders = headersConstructor.As<Napi::Function>().New({headers});
            const auto callback = Napi::Function::New(env, [&request](const Napi::CallbackInfo& info) {
                if (info.Length() >= 2)
                {
                    request.SetRequestHeader(info[1].ToString().Utf8Value(), info[0].ToString().Utf8Value());
                }
            });
            normalizedHeaders.Get("forEach").As<Napi::Function>().Call(normalizedHeaders, {callback});
        }

        void InitializeFetchClasses(Napi::Env env)
        {
            auto global = env.Global();
            const auto exports = Napi::Eval(env, FetchScripts::Polyfill, "jsruntimehost://fetch-polyfill.js").As<Napi::Object>();
            if (global.Get("Headers").IsUndefined())
            {
                global.Set("Headers", exports.Get("Headers"));
            }
            if (global.Get("Response").IsUndefined())
            {
                global.Set("Response", exports.Get("Response"));
            }
            JsRuntime::NativeObject::GetFromJavaScript(env).Set("createFetchResponse", exports.Get("createFetchResponse"));
        }

        template<typename THeaders>
        Napi::Value CreateFetchResponse(
            Napi::Env env,
            const void* body,
            size_t bodySize,
            const THeaders& headers,
            int status,
            std::string_view statusText,
            std::string_view url,
            bool redirected)
        {
            auto arrayBuffer = Napi::ArrayBuffer::New(env, bodySize);
            if (bodySize > 0)
            {
                std::memcpy(arrayBuffer.Data(), body, bodySize);
            }
            const auto bytes = Napi::Uint8Array::New(env, bodySize, arrayBuffer, 0);

            auto responseHeaders = Napi::Array::New(env, headers.size());
            uint32_t headerIndex{};
            for (const auto& header : headers)
            {
                auto pair = Napi::Array::New(env, 2);
                pair.Set(uint32_t{0}, Napi::String::New(env, header.first));
                pair.Set(uint32_t{1}, Napi::String::New(env, header.second));
                responseHeaders.Set(headerIndex++, pair);
            }

            auto init = Napi::Object::New(env);
            init.Set("headers", responseHeaders);
            init.Set("status", Napi::Number::New(env, status));
            init.Set("statusText", Napi::String::New(env, statusText.data(), statusText.size()));

            auto metadata = Napi::Object::New(env);
            metadata.Set("redirected", Napi::Boolean::New(env, redirected));
            metadata.Set("type", Napi::String::New(env, "basic"));
            metadata.Set("url", Napi::String::New(env, url.data(), url.size()));

            const auto nativeObject = JsRuntime::NativeObject::GetFromJavaScript(env);
            const auto createResponse = nativeObject.Get("createFetchResponse").As<Napi::Function>();
            return createResponse.Call(nativeObject, {env.Global().Get("Response"), bytes, init, metadata});
        }
    }

    namespace Fetch
    {
        void Initialize(Napi::Env env)
        {
            static constexpr auto JS_FETCH_NAME = "fetch";
            InitializeFetchClasses(env);

            auto fetchFunction = Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                Napi::Env env = info.Env();
                const auto deferred = Napi::Promise::Deferred::New(env);

                try
                {
                    if (info.Length() < 1)
                    {
                        throw std::runtime_error{"fetch requires at least 1 argument"};
                    }

                    // Resolve the request URL from a string, a Request-like object with a 'url', or anything stringifiable.
                    std::string url;
                    const Napi::Value input = info[0];
                    if (input.IsString())
                    {
                        url = input.As<Napi::String>().Utf8Value();
                    }
                    else if (input.IsObject() && input.As<Napi::Object>().Get("url").IsString())
                    {
                        url = input.As<Napi::Object>().Get("url").As<Napi::String>().Utf8Value();
                    }
                    else
                    {
                        url = input.ToString().Utf8Value();
                    }

                    UrlLib::UrlMethod method = UrlLib::UrlMethod::Get;
                    std::optional<std::string> body;
                    Napi::Value headers = env.Undefined();
                    Napi::Value signal = env.Undefined();

                    if (info.Length() > 1 && info[1].IsObject())
                    {
                        const auto init = info[1].As<Napi::Object>();

                        const auto methodValue = init.Get("method");
                        if (methodValue.IsString())
                        {
                            method = ParseMethod(methodValue.As<Napi::String>().Utf8Value());
                        }

                        const auto bodyValue = init.Get("body");
                        if (bodyValue.IsString())
                        {
                            body = bodyValue.As<Napi::String>().Utf8Value();
                        }
                        else if (!bodyValue.IsUndefined() && !bodyValue.IsNull())
                        {
                            throw std::runtime_error{"fetch: only string request bodies are supported"};
                        }

                        headers = init.Get("headers");
                        signal = init.Get("signal");
                    }

                    if (signal.IsObject())
                    {
                        const auto signalObject = signal.As<Napi::Object>();
                        if (signalObject.Get("aborted").ToBoolean().Value())
                        {
                            deferred.Reject(GetAbortReason(env, signalObject));
                            return deferred.Promise();
                        }
                    }

                    std::optional<DataUrlResponse> dataUrl;
                    try
                    {
                        dataUrl = ParseDataUrl(url);
                    }
                    catch (const std::runtime_error& error)
                    {
                        deferred.Reject(Napi::TypeError::New(env, error.what()).Value());
                        return deferred.Promise();
                    }

                    if (dataUrl)
                    {
                        if (method != UrlLib::UrlMethod::Get || body.has_value())
                        {
                            throw std::runtime_error{"fetch: data URLs only support GET requests"};
                        }
                        const std::vector<std::pair<std::string, std::string>> responseHeaders{
                            {"content-type", dataUrl->contentType}};
                        deferred.Resolve(CreateFetchResponse(
                            env,
                            dataUrl->body.data(),
                            dataUrl->body.size(),
                            responseHeaders,
                            200,
                            "OK",
                            dataUrl->url,
                            false));
                        return deferred.Promise();
                    }

                    auto request = std::make_shared<UrlLib::UrlRequest>();
                    request->Open(method, url);
                    request->ResponseType(UrlLib::UrlResponseType::Buffer);
                    ApplyRequestHeaders(*request, headers);
                    if (body)
                    {
                        request->SetRequestBody(std::move(*body));
                    }

                    // Snapshot the caller's stack now -- before SendAsync() moves work onto a worker
                    // thread -- so a transport-failure rejection can be attributed to the fetch() call
                    // site rather than to an empty scheduler tick.
                    const std::string capturedStack = CaptureCallSiteStack(env);

                    // Honor an AbortSignal passed via init.signal (WHATWG fetch). The signal is used
                    // through its JS interface (aborted / reason / add/removeEventListener) so fetch
                    // stays decoupled from the AbortController polyfill's C++ types.
                    std::shared_ptr<AbortState> abortState;
                    if (signal.IsObject())
                    {
                        const Napi::Object signalObject = signal.As<Napi::Object>();

                        // Already aborted: reject synchronously with the signal's reason, never
                        // touching the transport.
                        if (signalObject.Get("aborted").ToBoolean().Value())
                        {
                            deferred.Reject(GetAbortReason(env, signalObject));
                            return deferred.Promise();
                        }

                        abortState = std::make_shared<AbortState>();
                        abortState->signal = Napi::Persistent(signalObject);

                        Napi::Function listener = Napi::Function::New(env, [abortState, request, env](const Napi::CallbackInfo&) {
                            if (!abortState->aborted)
                            {
                                abortState->aborted = true;
                                abortState->reason = Napi::Persistent(GetAbortReason(env, abortState->signal.Value()));
                                // Cancel the in-flight transport; the completion continuation then
                                // rejects with the AbortError instead of a transport TypeError.
                                request->Abort();
                            }
                        });
                        abortState->listener = Napi::Persistent(listener);
                        signalObject.Get("addEventListener").As<Napi::Function>().Call(signalObject, {Napi::String::New(env, "abort"), listener});
                    }

                    // arcana::task::then captures the scheduler by reference (see arcana task.h) and
                    // invokes it on the worker thread when the request completes -- after this fetch()
                    // call has returned. A stack-local scheduler would therefore dangle. Heap-allocate
                    // it and co-own it from the continuation so it stays alive until the request finishes.
                    auto scheduler = std::make_shared<JsRuntimeScheduler>(JsRuntime::GetFromJavaScript(env));
                    request->SendAsync()
                        .then(*scheduler, arcana::cancellation::none(),
                            [deferred, request, env, url, capturedStack, abortState](const arcana::expected<void, std::exception_ptr>& result) {
                                // The request has settled: stop listening for aborts (breaking the
                                // listener <-> abortState ownership cycle) before deciding the outcome.
                                if (abortState)
                                {
                                    if (!abortState->signal.IsEmpty() && !abortState->listener.IsEmpty())
                                    {
                                        Napi::Object signalObject = abortState->signal.Value();
                                        signalObject.Get("removeEventListener").As<Napi::Function>().Call(signalObject, {Napi::String::New(env, "abort"), abortState->listener.Value()});
                                    }
                                    abortState->listener.Reset();
                                    abortState->signal.Reset();

                                    if (abortState->aborted)
                                    {
                                        // Per the fetch spec, an aborted request rejects with the
                                        // signal's reason (an AbortError), not a network error.
                                        deferred.Reject(abortState->reason.Value());
                                        return;
                                    }
                                }

                                const int status = static_cast<int>(request->StatusCode());

                                // Per the WHATWG fetch spec, only transport-level failures reject. A completed
                                // request with a non-2xx status (e.g. 404) still resolves with response.ok === false.
                                // A status of 0 indicates the transport never produced a response (network error).
                                if (result.has_error() || status == 0)
                                {
                                    // Reject with a TypeError carrying the normalized transport detail on `cause`
                                    // (built here, where the UrlRequest's ErrorString()/ErrorSymbol() are still in
                                    // scope) instead of throwing a constant string that discards them.
                                    deferred.Reject(BuildTransportError(env, *request, url, capturedStack).Value());
                                    return;
                                }

                                const auto responseBuffer = request->ResponseBuffer();
                                const auto statusText = request->StatusText();
                                const std::string responseUrl{request->ResponseUrl()};
                                const std::string_view finalUrl = responseUrl.empty() ? std::string_view{url} : std::string_view{responseUrl};
                                deferred.Resolve(CreateFetchResponse(
                                    env,
                                    responseBuffer.data(),
                                    responseBuffer.size(),
                                    request->GetAllResponseHeaders(),
                                    status,
                                    statusText,
                                    finalUrl,
                                    !responseUrl.empty() && responseUrl != url));
                            })
                        .then(*scheduler, arcana::cancellation::none(),
                            [deferred, env, scheduler](const arcana::expected<void, std::exception_ptr>& result) {
                                // A throw from the continuation above (e.g. a JS exception while building the
                                // response) lands here as an error result; surface it as a promise rejection so
                                // await fetch(...) settles. Transport failures are already rejected above, so this
                                // only handles unexpected exceptions. The scheduler is co-owned here so it
                                // outlives the in-flight request.
                                if (result.has_error())
                                {
                                    deferred.Reject(Napi::Error::New(env, result.error()).Value());
                                }
                            });
                }
                catch (...)
                {
                    deferred.Reject(Napi::Error::New(env, std::current_exception()).Value());
                }

                return deferred.Promise(); }, JS_FETCH_NAME);

            if (env.Global().Get(JS_FETCH_NAME).IsUndefined())
            {
                env.Global().Set(JS_FETCH_NAME, fetchFunction);
            }

            JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_FETCH_NAME, fetchFunction);
        }
    }
}

namespace Babylon::Polyfills::Fetch
{
    void BABYLON_API Initialize(Napi::Env env)
    {
        Internal::Fetch::Initialize(env);
    }
}
