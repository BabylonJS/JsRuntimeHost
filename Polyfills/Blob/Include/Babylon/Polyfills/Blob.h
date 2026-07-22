#pragma once

#include <napi/env.h>
#include <Babylon/Api.h>

#include <cstddef>
#include <string>

namespace Babylon::Polyfills::Blob
{
    void BABYLON_API Initialize(Napi::Env env);

    // Synchronously reads the bytes and MIME type of a Blob JS object created by this
    // polyfill. Returns false if `object` is not a Blob. The returned pointer remains
    // valid only while `object` is alive, so it must be consumed synchronously.
    bool BABYLON_API TryGetData(const Napi::Object& object, const std::byte*& outData, size_t& outSize, std::string& outType);
}
