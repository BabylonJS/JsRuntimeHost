# Worker

Provides a browser-compatible dedicated `Worker` backed by one `AppRuntime`
(and therefore one engine realm and native thread) per Worker.

Implemented surface:

- `new Worker(url, { name, type })`, `postMessage()`, and `terminate()`
- `WorkerGlobalScope` / `DedicatedWorkerGlobalScope`, `self`, `location`,
  `close()`, synchronous `importScripts()`, and worker `postMessage()`
- `EventTarget`, `Event`, `MessageEvent`, `ErrorEvent`, `DOMException`,
  `onmessage`, `onmessageerror`, and `onerror`
- structured cloning for cyclic objects, arrays, dates, regular expressions,
  maps, sets, errors, ArrayBuffers, DataViews, typed arrays, BigInts, and
  special number values
- transferable ArrayBuffers using the N-API v7 detach operation

The default loader reads relative paths and `app:///` URLs below
`Options::ScriptRoot`. Native applications can instead supply a thread-safe
`ScriptResolver` for packaged assets.

`type: "module"` accepts self-contained, script-compatible application bundles.
JavaScriptCore's public C API has no module-loader hook, so the application's
normal bundler must flatten top-level `import` and `export` declarations before
loading. Classic workers and bundled module workers share the same isolated
runtime.

On system JavaScriptCore, `terminate()` uses the engine execution-time-limit
symbol when the installed build exports it and can interrupt a tight loop.
Other engines, and older WebKitGTK builds without that symbol, currently stop
between dispatches until their native interrupt hooks are connected.

`WorkerGlobalScope.close()` stops future worker tasks but preserves messages
and uncaught errors produced later in the task that called it, as required by
WPT. Worker-owned runtime state is torn down on the worker thread; the final
strong reference to the parent-realm `Worker` object is released by a FIFO
dispatch on the parent runtime after worker engine/environment teardown.
