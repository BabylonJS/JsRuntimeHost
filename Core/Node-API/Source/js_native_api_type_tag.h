#pragma once

// [BABYLON-NATIVE-ADDITION]
//
// Shared helper for the QuickJS, Chakra and JavaScriptCore type-tag
// implementations.
//
// V8 stores the tag under a v8::Private, which script cannot reach at all.
// These three engines have no equivalent per-object native slot for an arbitrary
// object (JS_SetOpaque, JsSetExternalData and JSObjectSetPrivate all require an
// object of the port's own class), so each keeps a WeakMap that is reachable
// only from napi_env__ and stores the tag in it as the fixed-width hex string
// produced here. A hidden own property would not do: even under a symbol,
// Object.getOwnPropertySymbols hands script the key, and the tag could then be
// read off a real instance and replayed onto a spoofed object -- which is the
// type confusion the tag exists to prevent.

#include <napi/js_native_api_types.h>

#include <cstdint>

namespace napi_type_tag_util {

// 128 bits as hex, upper word first.
constexpr int kHexLength = 32;

inline void ToHex(const napi_type_tag* tag, char out[kHexLength + 1]) {
  constexpr char digits[] = "0123456789abcdef";
  const uint64_t words[2] = {tag->upper, tag->lower};
  for (int w = 0; w < 2; ++w) {
    for (int i = 0; i < 16; ++i) {
      out[w * 16 + i] = digits[(words[w] >> ((15 - i) * 4)) & 0xF];
    }
  }
  out[kHexLength] = '\0';
}

}  // namespace napi_type_tag_util
