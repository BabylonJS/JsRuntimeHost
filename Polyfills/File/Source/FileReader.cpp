#include "FileReader.h"

#include <cstring>
#include <memory>
#include <utility>

namespace Babylon::Polyfills::Internal
{
    namespace
    {
        constexpr auto JS_FILE_READER_CONSTRUCTOR_NAME = "FileReader";

        // Inlined RFC 4648 base64 encoder. We don't pull in a third-party
        // base64 library because no other JsRuntimeHost polyfill needs one
        // and this is the only call site.
        void EncodeBase64(const uint8_t* data, size_t size, std::string& out)
        {
            static constexpr char kAlphabet[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

            out.reserve(out.size() + ((size + 2) / 3) * 4);

            size_t i = 0;
            for (; i + 3 <= size; i += 3)
            {
                const uint32_t triple =
                    (static_cast<uint32_t>(data[i]) << 16) |
                    (static_cast<uint32_t>(data[i + 1]) << 8) |
                    static_cast<uint32_t>(data[i + 2]);
                out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
                out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
                out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
                out.push_back(kAlphabet[triple & 0x3F]);
            }

            if (i < size)
            {
                const uint32_t b0 = data[i];
                const uint32_t b1 = (i + 1 < size) ? data[i + 1] : 0u;
                const uint32_t triple = (b0 << 16) | (b1 << 8);

                out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
                out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
                out.push_back((i + 1 < size) ? kAlphabet[(triple >> 6) & 0x3F] : '=');
                out.push_back('=');
            }
        }

        constexpr const char* ON_HANDLERS[] = {
            "onloadstart",
            "onprogress",
            "onload",
            "onabort",
            "onerror",
            "onloadend",
        };

        Napi::Value MakeEvent(Napi::Env env, const Napi::Object& jsThis, const std::string& eventType)
        {
            // Compute loaded/total best-effort from the current result, mirroring
            // the JS polyfill that this C++ implementation replaces.
            double length = 0.0;
            auto result = jsThis.Get("result");
            if (result.IsArrayBuffer())
            {
                length = static_cast<double>(result.As<Napi::ArrayBuffer>().ByteLength());
            }
            else if (result.IsTypedArray())
            {
                length = static_cast<double>(result.As<Napi::TypedArray>().ByteLength());
            }
            else if (result.IsString())
            {
                length = static_cast<double>(result.As<Napi::String>().Utf8Value().size());
            }

            auto event = Napi::Object::New(env);
            event.Set("type", Napi::String::New(env, eventType));
            event.Set("target", jsThis);
            event.Set("currentTarget", jsThis);
            event.Set("lengthComputable", Napi::Boolean::New(env, false));
            event.Set("loaded", Napi::Number::New(env, length));
            event.Set("total", Napi::Number::New(env, length));
            return event;
        }
    }

    void FileReader::Initialize(Napi::Env env)
    {
        auto global = env.Global();

        if (!global.Get(JS_FILE_READER_CONSTRUCTOR_NAME).IsUndefined())
        {
            return;
        }

        // Expose EMPTY/LOADING/DONE both as static constants on the
        // constructor (FileReader.EMPTY) and as instance constants on the
        // prototype (new FileReader().EMPTY) per the WHATWG IDL.
        //
        // Important: do NOT set these via func.Get("prototype").Set(...) on
        // the returned constructor. On JSC, JSObjectMakeConstructor defaults
        // the constructor's .prototype property to Object.prototype, so
        // writing through it pollutes Object.prototype and breaks any
        // for..in over plain objects elsewhere in the runtime. The
        // InstanceValue descriptors below go through napi_define_class's
        // internal prototype lookup, which on JSC targets the napi-internal
        // prototype (distinct from .prototype) and on V8 targets the
        // function template's PrototypeTemplate — both correct, neither
        // touches Object.prototype.
        Napi::Function func = DefineClass(
            env,
            JS_FILE_READER_CONSTRUCTOR_NAME,
            {
                StaticValue("EMPTY", Napi::Number::New(env, EMPTY)),
                StaticValue("LOADING", Napi::Number::New(env, LOADING)),
                StaticValue("DONE", Napi::Number::New(env, DONE)),
                InstanceValue("EMPTY", Napi::Number::New(env, EMPTY)),
                InstanceValue("LOADING", Napi::Number::New(env, LOADING)),
                InstanceValue("DONE", Napi::Number::New(env, DONE)),
                InstanceMethod("readAsArrayBuffer", &FileReader::ReadAsArrayBuffer),
                InstanceMethod("readAsText", &FileReader::ReadAsText),
                InstanceMethod("readAsDataURL", &FileReader::ReadAsDataURL),
                InstanceMethod("readAsBinaryString", &FileReader::ReadAsBinaryString),
                InstanceMethod("abort", &FileReader::Abort),
                InstanceMethod("addEventListener", &FileReader::AddEventListener),
                InstanceMethod("removeEventListener", &FileReader::RemoveEventListener),
                InstanceMethod("dispatchEvent", &FileReader::DispatchEvent),
            });

        global.Set(JS_FILE_READER_CONSTRUCTOR_NAME, func);
    }

    FileReader::FileReader(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<FileReader>{info}
    {
        auto env = info.Env();
        auto jsThis = info.This().As<Napi::Object>();

        jsThis.Set("readyState", Napi::Number::New(env, EMPTY));
        jsThis.Set("result", env.Null());
        jsThis.Set("error", env.Null());

        // Initialize on* handler slots so they exist as enumerable, writable
        // properties (consumers commonly assign to them after construction).
        for (const auto* slot : ON_HANDLERS)
        {
            jsThis.Set(slot, env.Null());
        }
    }

    void FileReader::ReadAsArrayBuffer(const Napi::CallbackInfo& info)
    {
        StartRead(info, ReadMode::ArrayBuffer);
    }

    void FileReader::ReadAsText(const Napi::CallbackInfo& info)
    {
        StartRead(info, ReadMode::Text);
    }

    void FileReader::ReadAsDataURL(const Napi::CallbackInfo& info)
    {
        StartRead(info, ReadMode::DataUrl);
    }

    void FileReader::ReadAsBinaryString(const Napi::CallbackInfo& info)
    {
        StartRead(info, ReadMode::BinaryString);
    }

    void FileReader::Abort(const Napi::CallbackInfo& info)
    {
        auto env = info.Env();
        auto jsThis = info.This().As<Napi::Object>();

        auto state = jsThis.Get("readyState");
        if (!state.IsNumber() || state.As<Napi::Number>().Int32Value() != LOADING)
        {
            return;
        }

        // Bump the read token so the in-flight .then continuation in
        // StartRead() early-returns instead of clobbering state and
        // dispatching a phantom "load" after the user-initiated abort.
        m_readId++;

        jsThis.Set("readyState", Napi::Number::New(env, DONE));
        jsThis.Set("result", env.Null());
        jsThis.Set("error", Napi::Error::New(env, "FileReader aborted").Value());

        Dispatch(env, jsThis, "abort");
        Dispatch(env, jsThis, "loadend");
    }

    void FileReader::AddEventListener(const Napi::CallbackInfo& info)
    {
        if (info.Length() < 2 || !info[0].IsString() || !info[1].IsFunction())
        {
            return;
        }

        std::string eventType = info[0].As<Napi::String>().Utf8Value();
        Napi::Function handler = info[1].As<Napi::Function>();

        auto& list = m_eventHandlerRefs[eventType];
        for (const auto& existing : list)
        {
            if (existing.Value() == handler)
            {
                return;
            }
        }
        list.push_back(Napi::Persistent(handler));
    }

    void FileReader::RemoveEventListener(const Napi::CallbackInfo& info)
    {
        if (info.Length() < 2 || !info[0].IsString() || !info[1].IsFunction())
        {
            return;
        }

        std::string eventType = info[0].As<Napi::String>().Utf8Value();
        Napi::Function handler = info[1].As<Napi::Function>();

        auto it = m_eventHandlerRefs.find(eventType);
        if (it == m_eventHandlerRefs.end())
        {
            return;
        }

        auto& list = it->second;
        for (auto i = list.begin(); i != list.end(); ++i)
        {
            if (i->Value() == handler)
            {
                list.erase(i);
                return;
            }
        }
    }

    Napi::Value FileReader::DispatchEvent(const Napi::CallbackInfo& info)
    {
        auto env = info.Env();
        if (info.Length() == 0 || !info[0].IsObject())
        {
            return Napi::Boolean::New(env, false);
        }

        auto eventObj = info[0].As<Napi::Object>();
        auto typeValue = eventObj.Get("type");
        if (!typeValue.IsString())
        {
            return Napi::Boolean::New(env, false);
        }

        // Route through the internal Dispatch helper so on* handlers and
        // addEventListener listeners actually fire for the event type.
        Dispatch(env, info.This().As<Napi::Object>(), typeValue.As<Napi::String>().Utf8Value());
        return Napi::Boolean::New(env, true);
    }

    void FileReader::Dispatch(Napi::Env env, const Napi::Object& jsThis, const std::string& eventType)
    {
        auto event = MakeEvent(env, jsThis, eventType);

        const std::string onHandler = "on" + eventType;
        auto handler = jsThis.Get(onHandler);
        if (handler.IsFunction())
        {
            handler.As<Napi::Function>().Call(jsThis, {event});
            if (env.IsExceptionPending())
            {
                env.GetAndClearPendingException();
            }
        }

        auto it = m_eventHandlerRefs.find(eventType);
        if (it == m_eventHandlerRefs.end())
        {
            return;
        }

        // Snapshot the listener list so that mutations during dispatch
        // (e.g. a handler that calls removeEventListener) do not invalidate
        // the iterator we are walking.
        std::vector<Napi::Function> snapshot;
        snapshot.reserve(it->second.size());
        for (const auto& ref : it->second)
        {
            snapshot.push_back(ref.Value());
        }

        for (const auto& listener : snapshot)
        {
            listener.Call(jsThis, {event});
            if (env.IsExceptionPending())
            {
                env.GetAndClearPendingException();
            }
        }
    }

    void FileReader::StartRead(const Napi::CallbackInfo& info, ReadMode mode)
    {
        auto env = info.Env();
        auto jsThis = info.This().As<Napi::Object>();

        auto state = jsThis.Get("readyState");
        if (state.IsNumber() && state.As<Napi::Number>().Int32Value() == LOADING)
        {
            throw Napi::Error::New(env, "FileReader: read already in progress");
        }

        jsThis.Set("readyState", Napi::Number::New(env, LOADING));
        jsThis.Set("result", env.Null());
        jsThis.Set("error", env.Null());

        ++m_readId;
        const uint64_t myReadId = m_readId;

        Dispatch(env, jsThis, "loadstart");

        if (info.Length() == 0 || info[0].IsNull() || info[0].IsUndefined())
        {
            jsThis.Set("error", Napi::Error::New(env, "FileReader: argument is not a Blob").Value());
            jsThis.Set("readyState", Napi::Number::New(env, DONE));
            Dispatch(env, jsThis, "error");
            Dispatch(env, jsThis, "loadend");
            return;
        }

        Napi::Value source = info[0];
        Napi::Value promiseValue;
        std::string contentType = "application/octet-stream";

        if (source.IsObject())
        {
            auto sourceObj = source.As<Napi::Object>();

            auto typeVal = sourceObj.Get("type");
            if (typeVal.IsString())
            {
                auto t = typeVal.As<Napi::String>().Utf8Value();
                if (!t.empty())
                {
                    contentType = t;
                }
            }

            auto arrayBufferFn = sourceObj.Get("arrayBuffer");
            if (arrayBufferFn.IsFunction())
            {
                promiseValue = arrayBufferFn.As<Napi::Function>().Call(sourceObj, {});
            }
            else if (source.IsArrayBuffer())
            {
                auto promiseCtor = env.Global().Get("Promise").As<Napi::Object>();
                promiseValue = promiseCtor.Get("resolve").As<Napi::Function>().Call(promiseCtor, {source});
            }
        }

        if (!promiseValue.IsObject())
        {
            jsThis.Set("error", Napi::Error::New(env, "FileReader: argument has no arrayBuffer()").Value());
            jsThis.Set("readyState", Napi::Number::New(env, DONE));
            Dispatch(env, jsThis, "error");
            Dispatch(env, jsThis, "loadend");
            return;
        }

        // The .then() callbacks fire asynchronously, after StartRead() returns.
        // Capture a strong reference to the FileReader's JS wrapper so the
        // C++ ObjectWrap stays alive until the read settles even if the
        // user dropped their reference. ObjectReference is move-only, so
        // route it through shared_ptr to make the lambda copy-constructible
        // (Napi::Function::New stores via std::function).
        auto thisRef = std::make_shared<Napi::ObjectReference>(Napi::Persistent(jsThis));

        auto onResolve = Napi::Function::New(env,
            [this, myReadId, mode, contentType, thisRef](const Napi::CallbackInfo& cb) {
                Napi::Value buf = cb.Length() > 0 ? cb[0] : cb.Env().Null();
                HandleReadResult(myReadId, mode, contentType, thisRef->Value(), buf);
            });

        auto onReject = Napi::Function::New(env,
            [this, myReadId, thisRef](const Napi::CallbackInfo& cb) {
                Napi::Value err = cb.Length() > 0
                    ? cb[0]
                    : static_cast<Napi::Value>(Napi::Error::New(cb.Env(), "FileReader: unknown error").Value());
                HandleReadError(myReadId, thisRef->Value(), err);
            });

        auto promiseObj = promiseValue.As<Napi::Object>();
        promiseObj.Get("then").As<Napi::Function>().Call(promiseObj, {onResolve, onReject});
    }

    void FileReader::HandleReadResult(uint64_t myReadId, ReadMode mode, const std::string& contentType,
                                      Napi::Object jsThis, const Napi::Value& bufValue)
    {
        // Abort()-or-restart guard: the read token was bumped, so this
        // continuation belongs to a read that has been abandoned.
        if (m_readId != myReadId)
        {
            return;
        }
        auto state = jsThis.Get("readyState");
        if (!state.IsNumber() || state.As<Napi::Number>().Int32Value() != LOADING)
        {
            return;
        }

        auto env = jsThis.Env();

        if (!bufValue.IsArrayBuffer())
        {
            jsThis.Set("error", Napi::Error::New(env, "FileReader: source did not return an ArrayBuffer").Value());
            jsThis.Set("readyState", Napi::Number::New(env, DONE));
            Dispatch(env, jsThis, "error");
            Dispatch(env, jsThis, "loadend");
            return;
        }

        auto buffer = bufValue.As<Napi::ArrayBuffer>();
        const auto* data = static_cast<const uint8_t*>(buffer.Data());
        const size_t size = buffer.ByteLength();

        Napi::Value resultValue;
        switch (mode)
        {
            case ReadMode::ArrayBuffer:
            {
                resultValue = buffer;
                break;
            }
            case ReadMode::Text:
            {
                // Pass raw bytes to Napi::String::New, which constructs a
                // JS string from UTF-8 input. Replaces the JS polyfill's
                // chunked String.fromCharCode fallback.
                resultValue = Napi::String::New(env, reinterpret_cast<const char*>(data), size);
                break;
            }
            case ReadMode::DataUrl:
            {
                std::string b64;
                EncodeBase64(data, size, b64);
                std::string url;
                url.reserve(contentType.size() + b64.size() + 13);
                url.append("data:").append(contentType).append(";base64,").append(b64);
                resultValue = Napi::String::New(env, url);
                break;
            }
            case ReadMode::BinaryString:
            {
                // Byte-per-char Latin-1 mapping, matching the spec for
                // readAsBinaryString().
                std::string out;
                out.assign(reinterpret_cast<const char*>(data), size);
                resultValue = Napi::String::New(env, out);
                break;
            }
        }

        jsThis.Set("result", resultValue);
        jsThis.Set("readyState", Napi::Number::New(env, DONE));
        Dispatch(env, jsThis, "load");
        Dispatch(env, jsThis, "loadend");
    }

    void FileReader::HandleReadError(uint64_t myReadId, Napi::Object jsThis, const Napi::Value& error)
    {
        if (m_readId != myReadId)
        {
            return;
        }
        auto state = jsThis.Get("readyState");
        if (!state.IsNumber() || state.As<Napi::Number>().Int32Value() != LOADING)
        {
            return;
        }

        auto env = jsThis.Env();

        if (error.IsUndefined() || error.IsNull())
        {
            jsThis.Set("error", Napi::Error::New(env, "FileReader: unknown error").Value());
        }
        else
        {
            jsThis.Set("error", error);
        }
        jsThis.Set("readyState", Napi::Number::New(env, DONE));
        Dispatch(env, jsThis, "error");
        Dispatch(env, jsThis, "loadend");
    }
}
