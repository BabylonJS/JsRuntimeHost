#pragma once

#include <napi/js_native_api_types.h>

// Engine-agnostic pieces of the Node-API surface, implemented purely in terms
// of the public `napi_*` entry points so that every backend behaves the same.
// Backends whose engine offers a faithful native equivalent should keep using
// it; these helpers exist for the ones that do not.
namespace napi_shared {
  // Implements `napi_get_property_names` semantics: the names of all
  // enumerable string-keyed properties of `object` and of its prototype chain,
  // as an array of strings, matching a `for...in` enumeration.
  //
  // V8 gets this from a single `GetPropertyNames` call configured with
  // `kIncludePrototypes | ONLY_ENUMERABLE | SKIP_SYMBOLS`. JavaScriptCore,
  // Chakra and QuickJS have no equivalent, so this walks the prototype
  // chain explicitly. See https://github.com/BabylonJS/JsRuntimeHost/issues/216.
  //
  // `object` is coerced with `napi_coerce_to_object`, as V8's `CHECK_TO_OBJECT`
  // does. Callers are expected to have already validated `env` and `result`.
  napi_status GetEnumerablePropertyNames(napi_env env, napi_value object, napi_value* result);
}
