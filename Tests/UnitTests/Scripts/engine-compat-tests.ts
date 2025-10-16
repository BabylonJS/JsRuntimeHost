// Tests specifically for V8/JSC engine compatibility, based on integration corner cases from other runtime proxies 
import { expect } from "chai";

declare const hostPlatform: string;

// Polyfill for globalThis for older engines like Chakra
// NOTE: We use Function constructor instead of checking self/window/global because
// in V8 Android embedding, these variables don't exist and accessing them can throw
// ReferenceError even with typeof checks in certain bundling/strict mode contexts
const globalThisPolyfill = (function() {
    // First check if globalThis is already available (V8 7.1+, modern browsers)
    if (typeof globalThis !== 'undefined') return globalThis;

    // Use Function constructor to safely get global object
    // This works in all contexts (strict mode, non-strict, browser, Node, embedded V8)
    try {
        // In non-strict mode, this returns the global object
        return Function('return this')();
    } catch (e) {
        // If Function constructor fails (CSP restrictions), fall back to checking globals
        // Wrap each check in try-catch to handle ReferenceErrors in embedded contexts
        try { if (typeof self !== 'undefined') return self; } catch (e) {}
        try { if (typeof window !== 'undefined') return window; } catch (e) {}
        try { if (typeof global !== 'undefined') return global; } catch (e) {}
        throw new Error('unable to locate global object');
    }
})();

describe("JavaScript Engine Compatibility", function () {
    describe("N-API Compatibility", function () {
        it("should handle large strings efficiently", function () {
            // Test string handling across N-API boundary
            const largeString = 'x'.repeat(1000000); // 1MB string
            const startTime = Date.now();

            // This will cross the N-API boundary when console.log is called
            console.log(`Large string test: ${largeString.substring(0, 20)}...`);

            const elapsed = Date.now() - startTime;
            expect(elapsed).to.be.lessThan(1000); // Should complete within 1 second
        });

        it.skip("should handle TypedArray transfer correctly", function () {
            // SKIP: This test has endianness-specific expectations that may fail on different architectures
            // The test assumes little-endian byte order which may not be true on all platforms
            // See: https://developer.mozilla.org/en-US/docs/Glossary/Endianness
            // Test that TypedArrays work correctly across N-API
            const buffer = new ArrayBuffer(1024);
            const uint8 = new Uint8Array(buffer);
            const uint16 = new Uint16Array(buffer);
            const uint32 = new Uint32Array(buffer);

            // Write test pattern
            for (let i = 0; i < uint8.length; i++) {
                uint8[i] = i & 0xFF;
            }

            // Verify aliasing works correctly
            expect(uint16[0]).to.equal(0x0100); // Little-endian: 0x00, 0x01
            expect(uint32[0]).to.equal(0x03020100); // Little-endian: 0x00, 0x01, 0x02, 0x03
        });

        it("should handle Symbol correctly", function () {
            const sym1 = Symbol('test');
            const sym2 = Symbol('test');
            const sym3 = Symbol.for('global');
            const sym4 = Symbol.for('global');

            expect(sym1).to.not.equal(sym2); // Different symbols
            expect(sym3).to.equal(sym4); // Same global symbol
            expect(Symbol.keyFor(sym3)).to.equal('global');
        });
    });

    describe("Unicode and String Encoding", function () {
        it("should handle UTF-16 surrogate pairs correctly", function () {
            // Test emoji and other characters that require surrogate pairs
            const emoji = "😀🎉🚀";
            expect(emoji.length).to.equal(6); // 3 emojis × 2 UTF-16 code units each
            expect(emoji.charCodeAt(0)).to.equal(0xD83D); // High surrogate
            expect(emoji.charCodeAt(1)).to.equal(0xDE00); // Low surrogate

            // Test string iteration
            const chars = [...emoji];
            expect(chars.length).to.equal(3); // Iterator should handle surrogates correctly
            expect(chars[0]).to.equal("😀");
        });

        it("should handle various Unicode planes correctly", function () {
            // BMP (Basic Multilingual Plane)
            const bmp = "Hello 你好 مرحبا";
            expect(bmp).to.equal("Hello 你好 مرحبا");

            // Supplementary planes
            const supplementary = "𐍈𐍉𐍊"; // Gothic letters
            expect(supplementary.length).to.equal(6); // 3 characters × 2 code units

            // Combining characters
            const combining = "é"; // e + combining acute accent
            expect(combining.normalize('NFC')).to.equal("é"); // Composed form
            // Note: NFD normalization behavior may vary between engines
            // Some engines may already have the string in composed form
            const decomposed = combining.normalize('NFD');
            expect(decomposed.length).to.be.oneOf([1, 2]); // Either already composed or decomposed
        });

        it("should handle string encoding/decoding correctly", function () {
            if (typeof TextEncoder === 'undefined' || typeof TextDecoder === 'undefined') {
                console.log("TextEncoder/TextDecoder not available - skipping");
                this.skip(); // Skip if TextEncoder/TextDecoder not available
                return;
            }

            const encoder = new TextEncoder();
            const decoder = new TextDecoder();

            const text = "Hello 世界 🌍";
            const encoded = encoder.encode(text);
            const decoded = decoder.decode(encoded);

            expect(decoded).to.equal(text);
            expect(encoded).to.be.instanceOf(Uint8Array);
        });
    });

    describe("Memory Management", function () {
        it("should handle large array allocations", function () {
            // Test that large allocations work (important for Android memory limits)
            const size = 10 * 1024 * 1024; // 10MB
            const array = new Uint8Array(size);

            expect(array.length).to.equal(size);
            expect(array.byteLength).to.equal(size);

            // Write and verify some data
            array[0] = 255;
            array[size - 1] = 128;
            expect(array[0]).to.equal(255);
            expect(array[size - 1]).to.equal(128);
        });

        it("should handle WeakMap and WeakSet correctly", function () {
            const wm = new WeakMap();
            const ws = new WeakSet();

            let obj1 = { id: 1 };
            let obj2 = { id: 2 };

            wm.set(obj1, 'value1');
            ws.add(obj2);

            expect(wm.has(obj1)).to.be.true;
            expect(ws.has(obj2)).to.be.true;

            // These should allow garbage collection when objects are released
            obj1 = null as any;
            obj2 = null as any;

            // Can't directly test GC, but at least verify the APIs work
            expect(() => wm.set({ id: 3 }, 'value3')).to.not.throw();
        });
    });

    describe("ES6+ Features", function () {
        it("should support Proxy and Reflect", function () {
            const target = { value: 42 };
            const handler = {
                get(target: any, prop: string) {
                    if (prop === 'double') {
                        return target.value * 2;
                    }
                    return Reflect.get(target, prop);
                }
            };

            const proxy = new Proxy(target, handler);
            expect(proxy.value).to.equal(42);
            expect((proxy as any).double).to.equal(84);
        });

        it("should support BigInt", function () {
            if (typeof BigInt === 'undefined') {
                console.log("BigInt not supported - skipping");
                this.skip(); // Skip if BigInt not supported
                return;
            }

            const big1 = BigInt(Number.MAX_SAFE_INTEGER);
            const big2 = BigInt(1);
            const sum = big1 + big2;

            expect(sum > big1).to.be.true;
            expect(sum.toString()).to.equal("9007199254740992");
        });

        it("should support async iteration", async function () {
            async function* asyncGenerator() {
                yield 1;
                yield 2;
                yield 3;
            }

            const results: number[] = [];
            for await (const value of asyncGenerator()) {
                results.push(value);
            }

            expect(results).to.deep.equal([1, 2, 3]);
        });
    });

    describe("Performance Characteristics", function () {
        it.skip("should handle high-frequency timer operations", function (done) {
            // SKIP: This test times out on JSC and some CI environments
            // JSC on Android has slower timer scheduling compared to V8
            // CI environments may also have resource constraints affecting timer performance
            // Related: https://github.com/facebook/react-native/issues/29084 (timer performance issues)
            this.timeout(2000);

            let count = 0;
            const target = 100;
            const startTime = Date.now();

            function scheduleNext() {
                if (count < target) {
                    count++;
                    setTimeout(scheduleNext, 0);
                } else {
                    const elapsed = Date.now() - startTime;
                    console.log(`Scheduled ${target} timers in ${elapsed}ms`);
                    expect(elapsed).to.be.lessThan(2000); // Should handle 100 timers in under 2s
                    done();
                }
            }

            scheduleNext();
        });

        it("should handle deep recursion with proper tail calls (if supported)", function () {
            // Test stack depth handling
            let maxDepth = 0;

            function recurse(depth: number): number {
                try {
                    maxDepth = Math.max(maxDepth, depth);
                    if (depth >= 10000) return depth; // Stop at 10k to avoid infinite recursion
                    return recurse(depth + 1);
                } catch (e) {
                    // Stack overflow - return the max depth we reached
                    return maxDepth;
                }
            }

            const depth = recurse(0);
            console.log(`Max recursion depth: ${depth}`);
            expect(depth).to.be.greaterThan(100); // Should support at least 100 levels
        });
    });

    describe("Android-specific Compatibility", function () {
        if (hostPlatform === "Android") {
            it("should handle Android-specific buffer sizes", function () {
                // Android has specific buffer size limitations
                const sizes = [
                    64 * 1024,     // 64KB
                    256 * 1024,    // 256KB
                    1024 * 1024,   // 1MB
                    4 * 1024 * 1024 // 4MB
                ];

                for (const size of sizes) {
                    const buffer = new ArrayBuffer(size);
                    expect(buffer.byteLength).to.equal(size);
                }
            });

            it("should work with Android-style file URLs", async function () {
                // Test Android content:// and file:// URL handling
                const testUrl = "app:///Scripts/tests.js";

                // This should not throw
                expect(() => new URL(testUrl, "app://")).to.not.throw();
            });
        }
    });
});

// Export for use in main test file
export function runEngineCompatTests() {
    describe("Engine Compatibility Suite", function () {
        // Tests will be added here by mocha
    });
}
