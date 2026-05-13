# Console
Implements parts of [`console`](https://developer.mozilla.org/en-US/docs/Web/API/console). Provides a way to output debug messages from JavaScript into C++.

Currently supports:
* `log()`
* `warn()`
* `error()`

When initializing, you should provide a callback which takes a message, a log level, and a JS callstack and outputs the message in whatever way you like. The callstack is captured at the call site for `error()` calls (raw `Error.stack`, engine-defined format -- typically starting with a literal `Error\n` line); it is an empty string for `log()` and `warn()` calls (capturing a stack on every `console.log` would be too costly). For example, you could initialize it like so:
```c++
Babylon::Polyfills::Console::Initialize(env, [](const char* message, auto, const char* jsStack) {
    fprintf(stdout, "%s", message);
    if (jsStack[0] != '\0') {
        fprintf(stdout, "\n%s", jsStack);
    }
    fflush(stdout);
});
