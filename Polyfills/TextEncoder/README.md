# TextEncoder Polyfill

A C++ implementation of the [WHATWG Encoding API](https://encoding.spec.whatwg.org/) `TextEncoder` interface for use in Babylon Native JavaScript runtimes via [Napi](https://github.com/nodejs/node-addon-api).

This is the encoding counterpart to the `TextDecoder` polyfill in this same repository. Both polyfills exist primarily for older Chakra-based runtimes where the WHATWG Encoding Standard globals are not built in. On modern V8 / JSC / Hermes runtimes the constructor is already exposed and `Initialize()` is a no-op.

## Current State

### Supported

- Constructing `TextEncoder` with no argument (UTF-8 is the only encoding the spec mandates).
- The `encoding` accessor (always returns `"utf-8"`).
- `encode(input)` — returns a `Uint8Array` containing the UTF-8 bytes of `input`. Calling with no argument or `undefined` returns an empty `Uint8Array` (matches the spec, which defaults `input` to `""`).
- `encodeInto(source, destination)` — writes UTF-8 bytes of `source` into the caller-provided `Uint8Array` and returns `{ read, written }`. `read` is the number of UTF-16 code units consumed from `source` (4-byte UTF-8 sequences count as 2 because they correspond to a surrogate pair); multi-byte sequences are never split across the destination boundary.

### Not Supported

- Encodings other than UTF-8 — the `TextEncoder` constructor in the spec only accepts UTF-8 anyway, so this is not a deviation.
- `encodeInto` requires the destination to be a `Uint8Array` (passing any other typed array throws a `TypeError`).

## Usage

```javascript
const encoder = new TextEncoder();
encoder.encoding;                              // "utf-8"

encoder.encode("Hello");                       // Uint8Array(5) [72,101,108,108,111]
encoder.encode();                              // Uint8Array(0) []

const dst = new Uint8Array(8);
encoder.encodeInto("Hi", dst);                 // { read: 2, written: 2 }
encoder.encodeInto("\u{1F600}", dst);          // { read: 2, written: 4 } (surrogate pair, 4-byte UTF-8)
```
