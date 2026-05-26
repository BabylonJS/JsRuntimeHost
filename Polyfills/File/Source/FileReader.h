#pragma once

#include <napi/napi.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Babylon::Polyfills::Internal
{
    class FileReader final : public Napi::ObjectWrap<FileReader>
    {
    public:
        static constexpr int32_t EMPTY = 0;
        static constexpr int32_t LOADING = 1;
        static constexpr int32_t DONE = 2;

        static void Initialize(Napi::Env env);

        explicit FileReader(const Napi::CallbackInfo& info);

    private:
        enum class ReadMode
        {
            ArrayBuffer,
            Text,
            DataUrl,
            BinaryString,
        };

        void ReadAsArrayBuffer(const Napi::CallbackInfo& info);
        void ReadAsText(const Napi::CallbackInfo& info);
        void ReadAsDataURL(const Napi::CallbackInfo& info);
        void ReadAsBinaryString(const Napi::CallbackInfo& info);
        void Abort(const Napi::CallbackInfo& info);
        void AddEventListener(const Napi::CallbackInfo& info);
        void RemoveEventListener(const Napi::CallbackInfo& info);
        Napi::Value DispatchEvent(const Napi::CallbackInfo& info);

        void StartRead(const Napi::CallbackInfo& info, ReadMode mode);
        void HandleReadResult(uint64_t myReadId, ReadMode mode, const std::string& contentType,
                              Napi::Object jsThis, const Napi::Value& bufValue);
        void HandleReadError(uint64_t myReadId, Napi::Object jsThis, const Napi::Value& error);
        void Dispatch(Napi::Env env, const Napi::Object& jsThis, const std::string& eventType);

        uint64_t m_readId{0};
        std::unordered_map<std::string, std::vector<Napi::FunctionReference>> m_eventHandlerRefs;
    };
}
