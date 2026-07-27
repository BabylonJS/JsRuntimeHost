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
    // blob: URL. Consumers do not resolve these URLs directly: the URL polyfill registers a
    // process-global blob: resolver with UrlLib, so fetch, XMLHttpRequest, and any other UrlLib
    // consumer serve blob: URLs uniformly through the transport layer.
    //
    // The store is process-global (shared across all JS environments in the process); the minted
    // URLs embed a UUID so entries never collide between environments. It is process-global because
    // no Node-API adapter implements napi_add_env_cleanup_hook, so there is no portable hook on
    // which to free per-environment state (tracked by #215). Consequently entries outlive the
    // environment that created them: a browser drops its blob URL store at unload, which
    // corresponds to environment teardown here rather than process exit, so an embedder that
    // creates and destroys environments accumulates every un-revoked entry for the life of the
    // process. Call revokeObjectURL when done with a URL.
    //
    // The store shares the Blob's byte buffer through a shared_ptr, so registering a URL does not
    // duplicate it and resolving one hands out a reference rather than a copy. Bytes are released
    // once the entry is revoked and every outstanding resolver has dropped its shared_ptr, matching
    // how a browser Blob's bytes stay valid for an in-flight read even if the URL is revoked
    // mid-flight.

    // Registers `data` under a freshly minted blob: URL and returns it. The Blob's buffer is shared
    // rather than copied, so createObjectURL does not duplicate a large blob.
    // The `env` parameter is currently unused but kept for API symmetry and future per-env scoping.
    std::string BABYLON_API RegisterObjectURL(Napi::Env env, std::shared_ptr<const std::vector<std::byte>> data, std::string type);

    // Releases the entry for `url`, if any. Unknown URLs are ignored (matching the web platform).
    void BABYLON_API RevokeObjectURL(Napi::Env env, const std::string& url);
}
