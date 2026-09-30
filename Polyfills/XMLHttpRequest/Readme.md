# XMLHttpRequest
Minimal implementation of XMLHttpRequest required to support the Babylon.js RequestFile method. Under the hood, XMLHttpRequest is implemented using various platform-specific APIs in the UrlLib dependency.

## Event listening
We do not support `onload`-style event listeners. Instead, you should listen to events using `addEventListener`. At the moment, we only support the following events:
* `loadend`
* `readystatechange`
* `error` (fired on a transport failure or a non-`2xx` HTTP response, before `loadend`)

## Local files
Unlike the web, XMLHttpRequest supports loading local files using two schemes:
* `file:///` allows you to load from an absolute path
* `app:///` allows you to load from a relative path, either the current program or package depending on platform

## Other things to be aware of:
* Only `GET` requests are currently supported
* For `readyState`, we only support `UNSENT`, `OPENED`, and `DONE`
* If the platform transport rejects a URL during `open()`, the request still
  enters `OPENED`. Calling `send()` reports `DONE`, `error`, and `loadend`
  asynchronously, with `status === 0`. This lets asset loaders handle unsupported
  or scheme-less Native URLs through their error callbacks rather than aborting
  scene parsing. No document-relative URL resolution is added.
* Invalid methods, arguments, and unsupported request-body types still throw
  synchronously. A deferred URL-open failure exposes `errorCode === "UrlOpenFailed"`
  and the original error in `errorDetail`; reopening clears those diagnostics.
  Aborting its pending notification returns `readyState` to `UNSENT` without
  dispatching failure events; a new `open()` is required before another `send()`.

## Transport-error diagnostics (non-standard)
A transport-level failure surfaces the standard way -- an `error` event followed by `loadend`,
with `status === 0` -- exactly as on the web. In addition, two **non-standard, additive**
read-only properties expose the normalized `UrlLib` transport-error detail so BN-aware code can
tell a DNS failure from a refused connection or a missing local asset:
* `errorCode` -- the stable symbolic token (e.g. `"UrlOpenFailed"`, `"CURLE_COULDNT_CONNECT"`, `"NSURLErrorTimedOut"`,
  `"AppResourceNotFound"`)
* `errorDetail` -- the original opening error for `UrlOpenFailed`, or the normalized
  `"<domain>:<symbol>(<code>): <detail>"` string for a failure during `send()`

URL-opening errors populate both properties on every platform. Failures during
`send()` expose the diagnostics supplied by the platform's `UrlLib` backend;
those strings can be empty when the backend has no detail. Successful requests
leave both properties empty, and reopening clears an earlier opening error.
Browsers do not expose these properties, so spec-conformant code is unaffected.
