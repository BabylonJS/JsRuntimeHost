# AbortSignal
Implements parts of [`AbortSignal`](https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal). Implements a signal object in C++ that that can be aborted through the use of an AbortController object. *Work In Progress*

Currently not implemented:
* [`ThrowIfAborted`](https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal/throwIfAborted)
* [`Timeout`](https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal/timeout)
* [`Abort`](https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal/abort)

Both the AbortController and AbortSignal polyfills need to be initialized for the polyfills to work as expected:
```c++
Babylon::Polyfills::AbortController::Initialize(env);
Babylon::Polyfills::AbortSignal::Initialize(env);
