# AbortController
Implements parts of [`AbortController`](https://developer.mozilla.org/en-US/docs/Web/API/AbortController/) and [`AbortSignal`](https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal). Provides a way to trigger the abort signal. *Work In Progress*

The `JSRUNTIMEHOST_POLYFILL_ABORTCONTROLLER` CMake option (ON by default) builds both polyfills. The previous spelling, `JSRUNTIMEHOST_POLYFILL_ABORT_CONTROLLER`, is still accepted: `-DJSRUNTIMEHOST_POLYFILL_ABORT_CONTROLLER=OFF` disables the same target. If both names are set, they must agree; conflicting values fail configuration. When switching names in an existing build directory, remove the old cache entry or set both to the same value.

Supported on `AbortSignal`:
* `aborted` (read-only) and `reason`
* [`throwIfAborted()`](https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal/throwIfAborted)
* static [`AbortSignal.abort(reason?)`](https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal/abort)
* `onabort`, `addEventListener("abort", ...)`, `removeEventListener`

`AbortController.abort(reason?)` forwards the reason to the signal; when no reason is given the
signal's `reason` defaults to an `AbortError` (an `Error` whose `name` is `"AbortError"`, since
there is no `DOMException` polyfill). `fetch()` honors an `AbortSignal` passed via `init.signal`:
an already-aborted signal rejects the promise synchronously, and an in-flight abort cancels the
transport and rejects with the signal's `reason`. (Transport cancellation is effective on backends
where `UrlLib::UrlRequest::Abort()` is implemented.)

Currently not implemented:
* [`Timeout`](https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal/timeout)

Both the AbortController and AbortSignal polyfills are initialized inside AbortController's initialize method:
```c++
Babylon::Polyfills::AbortController::Initialize(env);
```
