# XMLHttpRequest
Minimal implementation of XMLHttpRequest required to support the Babylon.js RequestFile method. Under the hood, XMLHttpRequest is implemented using various platform-specific APIs in the UrlLib dependency.

## Event listening
Events can be observed with `addEventListener` or the corresponding
`onreadystatechange`, `onload`, `onerror`, `onloadend`, and `onabort` properties. Handlers receive
an event whose `target` and `currentTarget` are the request, and run with the request as `this`.
`readystatechange` dispatches an `Event`; `load`, `error`, `abort`, and `loadend` dispatch
`ProgressEvent` instances (`lengthComputable === false`, `loaded === total === 0`).
Missing constructors are installed without replacing host-provided ones.
At the moment, we only support the following events:
* `loadend`
* `readystatechange`
* `load` (fired after any completed HTTP response, including non-`2xx` responses)
* `error` (fired on a transport failure, before `loadend`)
* `abort` (fired when an active request is aborted, before `loadend`)

## Local files
Unlike the web, XMLHttpRequest supports loading local files using two schemes:
* `file:///` allows you to load from an absolute path
* `app:///` allows you to load from a relative path, either the current program or package depending on platform

## Other things to be aware of:
* Only `GET` requests are currently supported
* For `readyState`, we only support `UNSENT`, `OPENED`, and `DONE`

## Transport-error diagnostics (non-standard)
A transport-level failure surfaces the standard way -- an `error` event followed by `loadend`,
with `status === 0` -- exactly as on the web. In addition, two **non-standard, additive**
read-only properties expose the normalized `UrlLib` transport-error detail so BN-aware code can
tell a DNS failure from a refused connection or a missing local asset:
* `errorCode` -- the stable symbolic token (e.g. `"CURLE_COULDNT_CONNECT"`, `"NSURLErrorTimedOut"`,
  `"AppResourceNotFound"`)
* `errorDetail` -- the full normalized `"<domain>:<symbol>(<code>): <detail>"` string

Both are empty strings unless the request failed at the transport layer, and are populated only
on backends that expose the detail (Apple, Linux) -- empty on Windows/Android until those
backends populate `UrlLib`'s accessors. Browsers do not expose these properties, so
spec-conformant code is unaffected.
