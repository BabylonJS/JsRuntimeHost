'use strict';

// A small, non-proprietary reproduction of the Worker contract used by
// rebeckerspecialties/webapp's GithubPortfolio.worker.ts at commit
// 3765b131bca72057a4331ed7f466b9121cf24d1f. The deployed app constructs an
// ES-module Worker, creates an IndexedDB-backed pipeline, and moves multiple
// Date/Map-rich playback streams through postMessage.

const databasePromise = new Promise((resolve, reject) => {
  const request = indexedDB.open('visualization-worker-smoke');
  request.onupgradeneeded = () => {
    if (!request.result.objectStoreNames.contains('streams')) {
      request.result.createObjectStore('streams').put({ schema: 1 }, '__schema__');
    }
  };
  request.onsuccess = () => resolve(request.result);
  request.onerror = () => reject(request.error);
});

const keyFor = config => `${config.owner}/${config.repo}`;
const itemsPerStream = 128;
const makeStream = (recordType, weight) => ({
  recordType,
  items: Array.from({ length: itemsPerStream }, (_, index) => ({
    key: `${recordType}-${index}`,
    weight
  }))
});

onmessage = event => {
  const message = event.data;
  switch (message.type) {
    case 'pipelineCreate':
      databasePromise.then(database => {
        const transaction = database.transaction('streams', 'readwrite');
        transaction.objectStore('streams').put({
          currentDate: new Date('2026-07-21T00:00:00.000Z'),
          streams: [
            makeStream('commits', 3),
            makeStream('pulls', 2),
            makeStream('issues', 1)
          ]
        }, keyFor(message.config));
        transaction.oncomplete = () => {
          postMessage({ type: 'pipelineCreated', key: keyFor(message.config) });
        };
      });
      return;

    case 'subscribe':
      postMessage({ type: 'clone', value: false });
      postMessage({ type: 'currentDate', date: null });
      postMessage({ type: 'startFrom', date: undefined });
      postMessage({ type: 'playRate', playRate: 24 });
      postMessage({ type: 'configs', configs: [] });
      return;

    case 'pipelinePlay':
      databasePromise.then(database => {
        const request = database.transaction('streams').objectStore('streams').get(message.key);
        request.onsuccess = () => {
          const cached = request.result;
          const updatedItems = new Map();
          for (const stream of cached.streams) {
            for (const item of stream.items) updatedItems.set(item.key, item);
          }
          // Preserve an alias across the structured-clone graph, just as the
          // production diff envelope can reference an item from both data and
          // updatedItems.
          cached.streams[0].items[0].self = cached.streams[0].items[0];
          postMessage({
            type: 'data',
            data: cached.streams.map(stream => ({
              ...stream,
              currentDate: cached.currentDate
            })),
            updatedItems,
            metadata: {
              activeTypes: new Set(cached.streams.map(stream => stream.recordType))
            }
          });
          postMessage({ type: 'currentDate', date: cached.currentDate });
        };
      });
      return;
  }
};

postMessage({
  type: 'bootstrap',
  workerName: name,
  location: {
    href: location.href,
    protocol: location.protocol,
    pathname: location.pathname
  },
  globals: {
    indexedDB: typeof indexedDB,
    fetch: typeof fetch,
    AbortController: typeof AbortController,
    TextEncoder: typeof TextEncoder
  }
});
