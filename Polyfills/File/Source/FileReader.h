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
        };

        void ReadAsArrayBuffer(const Napi::CallbackInfo& info);
        void ReadAsText(const Napi::CallbackInfo& info);
        void ReadAsDataURL(const Napi::CallbackInfo& info);
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

        // Strong reference to the JS wrapper while a read is in flight, so
        // the C++ ObjectWrap stays alive across the async promise resolution
        // even if the user has dropped their JS-side reference. Reset on
        // every terminal path (load/error/abort). This matches the member-
        // slot pattern used by WebSocket/XHR in this repo and avoids the
        // shared_ptr<ObjectReference>-in-lambda trick that would otherwise
        // be needed because Napi::Function::New stores its callable in
        // std::function (CopyConstructible) and Napi::ObjectReference is
        // move-only.
        Napi::ObjectReference m_selfRef;
    };
}
