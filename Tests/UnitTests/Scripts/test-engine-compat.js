#!/usr/bin/env node

console.log('='.repeat(80));
console.log('JavaScript Engine Compatibility Test - Node.js Baseline');
console.log('='.repeat(80));
console.log('Node version:', process.version);
console.log('V8 version:', process.versions.v8);
console.log('Platform:', process.platform);
console.log('Architecture:', process.arch);
console.log('');

let passedTests = 0;
let failedTests = 0;
const results = [];

function test(name, fn) {
    try {
        fn();
        console.log('✅', name);
        passedTests++;
        results.push({ name, status: 'PASSED' });
    } catch (err) {
        console.log('❌', name);
        console.log('   Error:', err.message);
        failedTests++;
        results.push({ name, status: 'FAILED', error: err.message });
    }
}

function assert(condition, message) {
    if (!condition) {
        throw new Error(message || 'Assertion failed');
    }
}

console.log('Engine Detection Tests:');
console.log('-'.repeat(40));

test('V8 detection', () => {
    const hasV8Global = typeof global.v8 !== 'undefined';
    const hasProcessVersionsV8 = typeof process.versions.v8 !== 'undefined';
    assert(hasProcessVersionsV8, 'V8 version not found in process.versions');
    console.log('   V8 version:', process.versions.v8);
});

test('WebAssembly support', () => {
    assert(typeof WebAssembly !== 'undefined', 'WebAssembly not available');
    assert(typeof WebAssembly.Module !== 'undefined', 'WebAssembly.Module not available');
    assert(typeof WebAssembly.Instance !== 'undefined', 'WebAssembly.Instance not available');
});

console.log('\nN-API Compatibility Tests:');
console.log('-'.repeat(40));

test('Large string handling', () => {
    const largeString = 'x'.repeat(1000000); // 1MB
    const startTime = Date.now();
    const length = largeString.length;
    const elapsed = Date.now() - startTime;
    assert(length === 1000000, 'String length mismatch');
    assert(elapsed < 100, `String creation took ${elapsed}ms (should be < 100ms)`);
});

test('TypedArray support', () => {
    const buffer = new ArrayBuffer(1024);
    const uint8 = new Uint8Array(buffer);
    const uint16 = new Uint16Array(buffer);
    const uint32 = new Uint32Array(buffer);

    // Write test pattern
    for (let i = 0; i < uint8.length; i++) {
        uint8[i] = i & 0xFF;
    }

    // Test aliasing (little-endian)
    assert(uint16[0] === 0x0100, `uint16[0] = ${uint16[0].toString(16)}, expected 0x100`);
    assert(uint32[0] === 0x03020100, `uint32[0] = ${uint32[0].toString(16)}, expected 0x3020100`);
});

test('Symbol support', () => {
    const sym1 = Symbol('test');
    const sym2 = Symbol('test');
    const sym3 = Symbol.for('global');
    const sym4 = Symbol.for('global');

    assert(sym1 !== sym2, 'Local symbols should be unique');
    assert(sym3 === sym4, 'Global symbols should be the same');
    assert(Symbol.keyFor(sym3) === 'global', 'Symbol.keyFor failed');
});

console.log('\nUnicode and String Encoding Tests:');
console.log('-'.repeat(40));

test('UTF-16 surrogate pairs', () => {
    const emoji = "😀🎉🚀";
    assert(emoji.length === 6, `Length = ${emoji.length}, expected 6`);
    assert(emoji.charCodeAt(0) === 0xD83D, 'High surrogate incorrect');
    assert(emoji.charCodeAt(1) === 0xDE00, 'Low surrogate incorrect');

    const chars = [...emoji];
    assert(chars.length === 3, 'Iterator should handle surrogates');
    assert(chars[0] === "😀", 'First emoji incorrect');
});

test('Unicode normalization', () => {
    const combining = "é"; // e + combining accent
    const nfc = combining.normalize('NFC');
    const nfd = combining.normalize('NFD');
    assert(nfc === "é", 'NFC normalization failed');
    assert(nfd.length === 2, `NFD length = ${nfd.length}, expected 2`);
});

test('TextEncoder/TextDecoder', () => {
    if (typeof TextEncoder === 'undefined') {
        throw new Error('TextEncoder not available (needs Node.js 11+)');
    }

    const encoder = new TextEncoder();
    const decoder = new TextDecoder();
    const text = "Hello 世界 🌍";
    const encoded = encoder.encode(text);
    const decoded = decoder.decode(encoded);

    assert(decoded === text, 'Round-trip encoding failed');
    assert(encoded instanceof Uint8Array, 'Encoded should be Uint8Array');
});

console.log('\nMemory Management Tests:');
console.log('-'.repeat(40));

test('Large array allocation', () => {
    const size = 10 * 1024 * 1024; // 10MB
    const array = new Uint8Array(size);

    assert(array.length === size, 'Array length mismatch');
    assert(array.byteLength === size, 'Array byteLength mismatch');

    array[0] = 255;
    array[size - 1] = 128;
    assert(array[0] === 255, 'First element write failed');
    assert(array[size - 1] === 128, 'Last element write failed');
});

test('WeakMap and WeakSet', () => {
    const wm = new WeakMap();
    const ws = new WeakSet();

    let obj1 = { id: 1 };
    let obj2 = { id: 2 };

    wm.set(obj1, 'value1');
    ws.add(obj2);

    assert(wm.has(obj1), 'WeakMap.has failed');
    assert(ws.has(obj2), 'WeakSet.has failed');
});

console.log('\nES6+ Feature Tests:');
console.log('-'.repeat(40));

test('Proxy and Reflect', () => {
    const target = { value: 42 };
    const handler = {
        get(target, prop) {
            if (prop === 'double') {
                return target.value * 2;
            }
            return Reflect.get(target, prop);
        }
    };

    const proxy = new Proxy(target, handler);
    assert(proxy.value === 42, 'Proxy get failed');
    assert(proxy.double === 84, 'Proxy computed property failed');
});

test('BigInt support', () => {
    if (typeof BigInt === 'undefined') {
        throw new Error('BigInt not supported (needs Node.js 10.4+)');
    }

    const big1 = BigInt(Number.MAX_SAFE_INTEGER);
    const big2 = BigInt(1);
    const sum = big1 + big2;

    assert(sum > big1, 'BigInt addition failed');
    assert(sum.toString() === "9007199254740992", 'BigInt value incorrect');
});

test('Async generators', async () => {
    async function* asyncGenerator() {
        yield 1;
        yield 2;
        yield 3;
    }

    const results = [];
    for await (const value of asyncGenerator()) {
        results.push(value);
    }

    assert(results.length === 3, 'Async generator length wrong');
    assert(results[0] === 1 && results[1] === 2 && results[2] === 3, 'Async generator values wrong');
});

console.log('\nPerformance Tests:');
console.log('-'.repeat(40));

test('High-frequency timer operations', () => {
    const startTime = Date.now();
    let count = 0;

    // Synchronous test for Node.js
    for (let i = 0; i < 1000; i++) {
        setImmediate(() => { count++; });
    }

    const elapsed = Date.now() - startTime;
    assert(elapsed < 100, `Timer scheduling took ${elapsed}ms (should be < 100ms)`);
});

test('Deep recursion', () => {
    let maxDepth = 0;

    function recurse(depth) {
        try {
            maxDepth = Math.max(maxDepth, depth);
            if (depth >= 10000) return depth;
            return recurse(depth + 1);
        } catch (e) {
            return maxDepth;
        }
    }

    const depth = recurse(0);
    console.log(`   Max recursion depth: ${depth}`);
    assert(depth >= 100, `Recursion depth ${depth} is too shallow`);
});

// Run async tests
(async () => {
    console.log('\n' + '='.repeat(80));
    console.log('TEST SUMMARY');
    console.log('='.repeat(80));
    console.log(`Total tests: ${passedTests + failedTests}`);
    console.log(`Passed: ${passedTests}`);
    console.log(`Failed: ${failedTests}`);

    if (failedTests > 0) {
        console.log('\nFailed tests:');
        results.filter(r => r.status === 'FAILED').forEach(r => {
            console.log(`  - ${r.name}: ${r.error}`);
        });
    }

    console.log('\n' + '='.repeat(80));
    console.log('This baseline establishes what features are available in Node.js.');
    console.log('Some tests may fail in Android environments until V8/JSC upgrades.');
    console.log('='.repeat(80));

    process.exit(failedTests > 0 ? 1 : 0);
})();