# Pinned Worker WPT subset

`UPSTREAM_REVISION` records the exact
[web-platform-tests/wpt](https://github.com/web-platform-tests/wpt) commit used
for this directory. `LICENSE.md`, `resources/testharness.js`, and the files
below are copied from that revision:

- `workers/interfaces/DedicatedWorkerGlobalScope/EventTarget.worker.js`
- `workers/interfaces/DedicatedWorkerGlobalScope/onmessage.worker.js`
- `workers/interfaces/DedicatedWorkerGlobalScope/postMessage/return-value.worker.js`
- `workers/interfaces/WorkerGlobalScope/self.any.js`
- `workers/interfaces/WorkerUtils/importScripts/001.worker.js`
- `workers/support/Worker-structure-message.js`
- `workers/constructors/Worker/terminate.js`

`self.worker.js` is a local worker-harness wrapper for `self.any.js`.
The following focused lifecycle regressions are adapted from newer WPT and
browser-engine fixes; each source file carries its exact upstream links:

- `workers/support/WorkerGlobalScope-close.js` checks that `close()` preserves
  same-task messages/errors while discarding later tasks.
- `workers/support/Worker-early-message.js` checks the startup queue and the
  complementary terminate-before-start cleanup path.
- `workers/support/Worker-run-forever.js` checks JSC interruption during
  top-level evaluation, based on WPT's terminate-during-evaluation regression.
- `workers/support/Worker-termination-stress.js` repeats shutdown with an
  inbound task pending, guarding the cross-thread destruction pattern fixed by
  WebKit in July 2026.
- `workers/support/visualization-worker-smoke.js` is a small, non-proprietary
  reproduction of the deployed rebeckerspecialties visualization worker's
  startup contract. It constructs a named module-compatible worker from a
  WHATWG `URL`, opens the IndexedDB cache used by `pipelineCreate`, queues the
  interop startup messages, and sends multiple `Date`/`Map`/`Set`-rich
  playback streams through structured clone.

`runner.js` replaces WPT's browser document/server runner: it launches the
worker tests sequentially, consumes `testharness.js` completion records, and
adapts the document-side structured-clone, transfer, startup, close, and
termination checks to the JsRuntimeHost unit-test host. The infinite-evaluation
case runs only when the selected engine has a native execution-interrupt hook;
the rest of the subset remains cross-engine. The visualization smoke case is
run last so it exercises a fresh worker after the lifecycle stress cases.
