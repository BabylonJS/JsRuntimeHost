#pragma once

#include <napi/napi.h>

#include <vector>
#include <string>

namespace Babylon::Polyfills::Internal
{
    class Blob : public Napi::ObjectWrap<Blob>
    {
    public:
        static void Initialize(Napi::Env env);

        explicit Blob(const Napi::CallbackInfo& info);

        // Synchronous accessors for internal cross-polyfill use (e.g. URL.createObjectURL).
        const std::vector<std::byte>& Data() const { return m_data; }
        const std::string& Type() const { return m_type; }

    private:
        Napi::Value GetSize(const Napi::CallbackInfo& info);
        Napi::Value GetType(const Napi::CallbackInfo& info);
        Napi::Value Text(const Napi::CallbackInfo& info);
        Napi::Value ArrayBuffer(const Napi::CallbackInfo& info);
        Napi::Value Bytes(const Napi::CallbackInfo& info);

        void ProcessBlobPart(const Napi::Value& blobPart);

        std::vector<std::byte> m_data;
        std::string m_type;
    };
}