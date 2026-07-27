#pragma once

#include <napi/env.h>
#include <Babylon/Api.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Babylon::Polyfills::Blob
{
    void BABYLON_API Initialize(Napi::Env env);

    // Synchronously reads the bytes and MIME type of a Blob JS object created by this polyfill.
    // Returns false if `object` is not a Blob. A Blob is immutable once constructed, so `outData`
    // is a shared reference to its buffer rather than a copy, and it stays valid for as long as the
    // caller holds it -- even if `object` is collected.
    bool BABYLON_API TryGetData(const Napi::Object& object, std::shared_ptr<const std::vector<std::byte>>& outData, std::string& outType);
}
