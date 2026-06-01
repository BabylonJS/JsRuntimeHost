#include <Babylon/Polyfills/TextEncoder.h>

#include <napi/napi.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace
{
    // Number of UTF-16 code units that the given (validated) UTF-8 leading byte
    // represents. Returns 0 for a continuation byte. A 4-byte UTF-8 sequence
    // encodes a code point outside the BMP, which corresponds to a surrogate
    // pair in UTF-16 (2 code units); everything else is 1 code unit.
    inline uint32_t Utf8LeadingByteToCodeUnits(uint8_t b)
    {
        if ((b & 0x80u) == 0u)     return 1; // ASCII
        if ((b & 0xE0u) == 0xC0u)  return 1; // 2-byte sequence
        if ((b & 0xF0u) == 0xE0u)  return 1; // 3-byte sequence
        if ((b & 0xF8u) == 0xF0u)  return 2; // 4-byte sequence -> surrogate pair
        return 0;                            // continuation or invalid
    }

    inline size_t Utf8SequenceLength(uint8_t b)
    {
        if ((b & 0x80u) == 0u)     return 1;
        if ((b & 0xE0u) == 0xC0u)  return 2;
        if ((b & 0xF0u) == 0xE0u)  return 3;
        if ((b & 0xF8u) == 0xF0u)  return 4;
        return 1;
    }

    class TextEncoder final : public Napi::ObjectWrap<TextEncoder>
    {
    public:
        static void Initialize(Napi::Env env)
        {
            Napi::HandleScope scope{env};

            static constexpr auto JS_TEXTENCODER_CONSTRUCTOR_NAME = "TextEncoder";
            if (env.Global().Get(JS_TEXTENCODER_CONSTRUCTOR_NAME).IsUndefined())
            {
                Napi::Function func = DefineClass(
                    env,
                    JS_TEXTENCODER_CONSTRUCTOR_NAME,
                    {
                        InstanceAccessor("encoding", &TextEncoder::Encoding, nullptr),
                        InstanceMethod("encode", &TextEncoder::Encode),
                        InstanceMethod("encodeInto", &TextEncoder::EncodeInto),
                    });

                env.Global().Set(JS_TEXTENCODER_CONSTRUCTOR_NAME, func);
            }
        }

        explicit TextEncoder(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<TextEncoder>{info}
        {
            // The TextEncoder constructor takes no arguments. The encoding is
            // always UTF-8 per the WHATWG Encoding Standard.
        }

    private:
        Napi::Value Encoding(const Napi::CallbackInfo& info)
        {
            return Napi::String::New(info.Env(), "utf-8");
        }

        // Coerce arg 0 to a std::string of UTF-8 bytes. Per spec, undefined
        // input defaults to the empty string (not "undefined").
        static std::string InputAsUtf8(const Napi::CallbackInfo& info)
        {
            if (info.Length() < 1 || info[0].IsUndefined())
            {
                return {};
            }
            return info[0].ToString().Utf8Value();
        }

        // encode(input = "") - returns a Uint8Array containing the UTF-8 bytes.
        Napi::Value Encode(const Napi::CallbackInfo& info)
        {
            auto env = info.Env();
            auto utf8 = InputAsUtf8(info);

            auto bytes = Napi::Uint8Array::New(env, utf8.size());
            if (!utf8.empty())
            {
                std::memcpy(bytes.Data(), utf8.data(), utf8.size());
            }
            return bytes;
        }

        // encodeInto(source, destination) - writes UTF-8 bytes into a caller-
        // provided Uint8Array and returns { read, written }. Per the WHATWG
        // Encoding Standard, "read" is the number of UTF-16 code units consumed
        // from `source` and multi-byte sequences are never split.
        Napi::Value EncodeInto(const Napi::CallbackInfo& info)
        {
            auto env = info.Env();
            if (info.Length() < 2 || !info[1].IsTypedArray())
            {
                throw Napi::TypeError::New(env, "TextEncoder.encodeInto: destination must be a Uint8Array");
            }
            auto dest = info[1].As<Napi::Uint8Array>();

            auto utf8 = InputAsUtf8(info);

            // Walk forward through the UTF-8 bytes one code-point sequence at a
            // time, stopping when the next sequence would not fit. This keeps
            // multi-byte sequences atomic and lets us derive an accurate UTF-16
            // "read" count without ever building a separate UTF-16 string.
            size_t writeOffset = 0;
            uint32_t readCodeUnits = 0;
            const size_t capacity = dest.ByteLength();
            const uint8_t* src = reinterpret_cast<const uint8_t*>(utf8.data());

            for (size_t i = 0; i < utf8.size();)
            {
                size_t seqLen = Utf8SequenceLength(src[i]);
                if (i + seqLen > utf8.size())
                {
                    break;
                }
                if (writeOffset + seqLen > capacity)
                {
                    break;
                }
                readCodeUnits += Utf8LeadingByteToCodeUnits(src[i]);
                writeOffset += seqLen;
                i += seqLen;
            }

            if (writeOffset > 0)
            {
                std::memcpy(dest.Data(), utf8.data(), writeOffset);
            }

            auto result = Napi::Object::New(env);
            result.Set("read", Napi::Number::New(env, static_cast<double>(readCodeUnits)));
            result.Set("written", Napi::Number::New(env, static_cast<double>(writeOffset)));
            return result;
        }
    };
}

namespace Babylon::Polyfills::TextEncoder
{
    void BABYLON_API Initialize(Napi::Env env)
    {
        ::TextEncoder::Initialize(env);
    }
}
