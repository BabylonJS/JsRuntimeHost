#pragma once

#include <Babylon/Api.h>
#include <napi/env.h>

#include <functional>
#include <string>

namespace Babylon::Polyfills::DynamicScriptLoader
{
    // Return source as a JavaScript string or a Promise resolving to one.
    // Return null/undefined (or reject) when the resource is unavailable.
    // Called on the JavaScript thread; any asynchronous completion must also use
    // the runtime dispatcher before accessing Node-API values.
    using ResolverT = std::function<Napi::Value BABYLON_API (Napi::Env, const std::string&)>;

    // Installs loadScript(name): Promise<void>. The resolver alone determines
    // which resources can be loaded; no file or network fallback is provided.
    void BABYLON_API Initialize(Napi::Env env, ResolverT resolver);
}
