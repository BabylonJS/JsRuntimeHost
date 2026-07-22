#pragma once

#include <napi/env.h>
#include <Babylon/Api.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Babylon::Polyfills::URL
{
    void BABYLON_API Initialize(Napi::Env env);

    // Blob URL store backing URL.createObjectURL / URL.revokeObjectURL. Native has no browser
    // blob: URL store, so the URL polyfill keeps an in-memory registry keyed by a minted
    // blob: URL. The XMLHttpRequest and fetch polyfills call TryResolveObjectURL to serve those
    // URLs from memory instead of handing them to the (scheme-unaware) transport.

    // Copies `size` bytes into the per-environment store and returns a freshly minted blob: URL.
    std::string BABYLON_API RegisterObjectURL(Napi::Env env, const std::byte* data, size_t size, std::string type);

    // Releases the entry for `url`, if any. Unknown URLs are ignored (matching the web platform).
    void BABYLON_API RevokeObjectURL(Napi::Env env, const std::string& url);

    // Copies the bytes and MIME type registered for `url` into the out-parameters. Returns false
    // if `url` is not a live blob: URL in this environment's store.
    bool BABYLON_API TryResolveObjectURL(Napi::Env env, const std::string& url, std::vector<std::byte>& outData, std::string& outType);
}
