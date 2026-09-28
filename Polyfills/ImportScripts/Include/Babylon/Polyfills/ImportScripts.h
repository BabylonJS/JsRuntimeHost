#pragma once

#include <Babylon/Api.h>
#include <napi/env.h>

#include <functional>
#include <optional>
#include <string>

namespace Babylon::Polyfills::ImportScripts
{
    // Return source for an explicitly packaged script, or nullopt when it is unavailable.
    // The resolver runs synchronously on the JavaScript thread and must not load arbitrary files.
    using ResolverT = std::function<std::optional<std::string> BABYLON_API (const std::string&)>;

    // Install importScripts(...names) for bundles split by a bundler. Resource
    // names must be strings; each is evaluated in order in the current context.
    // This does not enable native ES module syntax in engines such as Chakra.
    void BABYLON_API Initialize(Napi::Env env, ResolverT resolver);
}
