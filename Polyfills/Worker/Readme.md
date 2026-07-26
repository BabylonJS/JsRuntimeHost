# Worker

Provides a browser-compatible dedicated `Worker` backed by one `AppRuntime`
(and therefore one engine realm and native thread) per Worker.

Implemented surface:

- `new Worker(url, { name, type })`, `postMessage()`, and `terminate()`
- `WorkerGlobalScope` / `DedicatedWorkerGlobalScope`, `self`, `location`,
  `close()`, synchronous `importScripts()`, and worker `postMessage()`
- `EventTarget`, `Event`, `MessageEvent`, `ErrorEvent`, `DOMException`,
  `onmessage`, `onmessageerror`, and `onerror`
- the cache-oriented IndexedDB subset commonly used by worker bundles:
  `open()`, database/object-store creation, read/write transactions, and
  asynchronous `get`, `getAll`, `count`, `put`, `add`, `delete`, and `clear`
- worker-relative string and `URL` inputs to `fetch()`, plus the buffered
  `Blob.stream()` / `ReadableStream` / `DecompressionStream` / `Response`
  pipeline used to hydrate gzip-compressed application assets
- structured cloning for cyclic objects, arrays, dates, regular expressions,
  maps, sets, errors, ArrayBuffers, DataViews, typed arrays, BigInts, and
  special number values
- transferable ArrayBuffers using the N-API v7 detach operation

The default loader reads relative paths and `app:///` URLs below
`Options::ScriptRoot`. Native applications can instead supply a thread-safe
`ScriptResolver` for packaged assets.

WHATWG `URL` serializes a hostless `app:///worker.js` URL as
`app:/worker.js`; both spellings resolve through `ScriptRoot`. Worker
`location` exposes the URL fields application bundles normally inspect
(`protocol`, `origin`, `pathname`, and related fields), not only `href`.
Native JavaScriptCore class objects are normalized inside the Worker realm so
browser-style constructor feature checks such as
`typeof AbortController === "function"` behave as expected.

The built-in IndexedDB subset is intentionally in-memory and scoped to one
Worker lifetime. It unblocks browser cache clients, including the visualization
integration fixture. Applications that require durable storage, indexes,
cursors, or cross-realm database sharing should install a complete host storage
implementation.

The built-in streams layer is likewise a compatibility subset: fetch and Blob
bodies are already buffered by JsRuntimeHost, so it represents each body as one
chunk and performs gzip/deflate decompression in the dedicated worker realm.
It supports body readers and `pipeThrough(new DecompressionStream(...))`, but
does not yet implement general streaming backpressure or arbitrary
`TransformStream` / `WritableStream` producers. Inflated bodies are capped at
512 MiB.

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
