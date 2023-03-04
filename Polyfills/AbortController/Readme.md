# AbortController
Implements parts of [`AbortController`](https://developer.mozilla.org/en-US/docs/Web/API/AbortController/). Provides a way to trigger the abort signal. *Work In Progress*


Both the AbortController and AbortSignal polyfills need to be initialized for the polyfills to work as expected:
```c++
Babylon::Polyfills::AbortController::Initialize(env);
Babylon::Polyfills::AbortSignal::Initialize(env);