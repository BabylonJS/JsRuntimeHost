#pragma once

#include <napi/env.h>
#include <Babylon/Api.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Babylon::Polyfills::URL
{
    void BABYLON_API Initialize(Napi::Env env);

    // Blob URL store backing URL.createObjectURL / URL.revokeObjectURL. Native has no browser
    // blob: URL store, so the URL polyfill keeps an in-memory registry keyed by a minted
    // blob: URL. The store is process-global (shared across all JS environments in the process);
    // the minted URLs embed a UUID so entries never collide between environments. The
    // XMLHttpRequest and fetch polyfills call TryResolveObjectURL to serve those URLs from memory
    // instead of handing them to the (scheme-unaware) transport.
    //
    // The store holds the Blob's bytes in a shared_ptr, so resolving a blob: URL hands out a
    // reference to the immutable buffer rather than copying it. Bytes are released once the entry
    // is revoked and every outstanding resolver has dropped its shared_ptr, matching how a browser
    // Blob's bytes stay valid for an in-flight read even if the URL is revoked mid-flight.

    // Copies `size` bytes into the process-global store and returns a freshly minted blob: URL.
    // The `env` parameter is currently unused but kept for API symmetry and future per-env scoping.
    std::string BABYLON_API RegisterObjectURL(Napi::Env env, const std::byte* data, size_t size, std::string type);

    // Releases the entry for `url`, if any. Unknown URLs are ignored (matching the web platform).
    void BABYLON_API RevokeObjectURL(Napi::Env env, const std::string& url);

    // Returns a shared handle to the immutable bytes registered for `url`, and writes the MIME type
    // into `outType`. Returns nullptr if `url` is not a live blob: URL in the process-global store.
    // The returned buffer is shared (not copied) and remains valid for as long as the caller holds
    // the shared_ptr, even if the entry is revoked in the meantime.
    std::shared_ptr<const std::vector<std::byte>> BABYLON_API TryResolveObjectURL(Napi::Env env, const std::string& url, std::string& outType);
}
