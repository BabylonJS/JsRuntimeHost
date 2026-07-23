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
    // the minted URLs embed a UUID so entries never collide between environments. Consumers do not
    // resolve these URLs directly: the URL polyfill registers a process-global blob: resolver with
    // UrlLib, so fetch, XMLHttpRequest, and any other UrlLib consumer serve blob: URLs uniformly
    // through the transport layer.
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
}
