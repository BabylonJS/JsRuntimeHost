'use strict';
// Flags: --expose-gc

// Regression coverage adapted from microsoft/hermes-windows#349, with the
// weak-reference creation case from #321 kept on the N-API v5 surface.
// https://github.com/microsoft/hermes-windows/pull/349
// https://github.com/microsoft/hermes-windows/pull/321
const { buildType } = require('../../common');
const { gcUntil } = require('../../common/gc');
const assert = require('assert');

const metadata = require(
  `./build/${buildType}/test_hermes_private_metadata`);

function makeThrowingProxy() {
  let trapCount = 0;
  const unexpected = () => {
    trapCount++;
    throw new Error('Node-API metadata must not invoke a Proxy trap');
  };
  const proxy = new Proxy({}, {
    get: unexpected,
    getOwnPropertyDescriptor: unexpected,
    defineProperty: unexpected,
    deleteProperty: unexpected,
    has: unexpected,
    set: unexpected,
  });
  return { proxy, getTrapCount: () => trapCount };
}

{
  const { proxy, getTrapCount } = makeThrowingProxy();
  assert.strictEqual(metadata.weakReferenceRoundTrip(proxy), true);
  assert.strictEqual(metadata.wrapRoundTrip(proxy), true);
  assert.strictEqual(getTrapCount(), 0);
}

{
  const object = Object.freeze({});
  assert.strictEqual(metadata.weakReferenceRoundTrip(object), true);
  assert.strictEqual(metadata.wrapRoundTrip(object), true);
}

{
  const parent = {};
  const child = Object.create(parent);
  assert.strictEqual(metadata.wrapPrototypeIsolation(parent, child), true);
}

(async function testProxyFinalizer() {
  const { getTrapCount } = (() => {
    const { proxy, getTrapCount } = makeThrowingProxy();
    metadata.addFinalizer(proxy);
    return { getTrapCount };
  })();

  await gcUntil(
    'Node-API finalizer on proxy',
    () => metadata.finalizeCount === 1);
  assert.strictEqual(getTrapCount(), 0);
})();
