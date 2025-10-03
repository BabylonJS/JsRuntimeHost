#!/usr/bin/env node

// Mock the host platform for local testing
global.hostPlatform = "macOS";
global.setExitCode = (code) => process.exit(code);

// Mock browser-like environment
global.window = global;
global.location = { href: '' };

// Mock some basic polyfills if needed
if (typeof globalThis.AbortController === 'undefined') {
    class AbortController {
        constructor() {
            this.signal = {
                aborted: false,
                onabort: null,
                addEventListener: () => {},
                removeEventListener: () => {}
            };
        }
        abort() {
            this.signal.aborted = true;
            if (this.signal.onabort) this.signal.onabort();
        }
    }
    global.AbortController = AbortController;
}

// Simple XMLHttpRequest mock
if (typeof globalThis.XMLHttpRequest === 'undefined') {
    class XMLHttpRequest {
        constructor() {
            this.readyState = 0;
            this.status = 0;
            this.responseText = '';
            this.response = null;
        }
        open() { this.readyState = 1; }
        send() {
            setTimeout(() => {
                this.readyState = 4;
                this.status = 200;
                if (this.onloadend) this.onloadend();
            }, 10);
        }
        addEventListener(event, handler) {
            this['on' + event] = handler;
        }
    }
    global.XMLHttpRequest = XMLHttpRequest;
}

// WebSocket mock
if (typeof globalThis.WebSocket === 'undefined') {
    class WebSocket {
        constructor(url) {
            this.url = url;
            this.readyState = 0;
            setTimeout(() => {
                this.readyState = 1;
                if (this.onopen) this.onopen();
            }, 10);
        }
        send(data) {
            setTimeout(() => {
                if (this.onmessage) this.onmessage({ data });
            }, 10);
        }
        close() {
            this.readyState = 3;
            if (this.onclose) this.onclose();
        }
    }
    global.WebSocket = WebSocket;
}

// URL and URLSearchParams are available in Node.js 10+
// Blob is available in Node.js 15+
if (typeof globalThis.Blob === 'undefined') {
    class Blob {
        constructor(parts, options = {}) {
            this.type = options.type || '';
            this.size = 0;
            this._content = '';

            if (parts) {
                for (const part of parts) {
                    if (typeof part === 'string') {
                        this._content += part;
                        this.size += part.length;
                    } else if (part instanceof Uint8Array) {
                        this._content += String.fromCharCode(...part);
                        this.size += part.length;
                    } else if (part instanceof ArrayBuffer) {
                        const view = new Uint8Array(part);
                        this._content += String.fromCharCode(...view);
                        this.size += view.length;
                    } else if (part instanceof Blob) {
                        this._content += part._content;
                        this.size += part.size;
                    }
                }
            }
        }

        async text() {
            return this._content;
        }

        async arrayBuffer() {
            const buffer = new ArrayBuffer(this._content.length);
            const view = new Uint8Array(buffer);
            for (let i = 0; i < this._content.length; i++) {
                view[i] = this._content.charCodeAt(i);
            }
            return buffer;
        }

        async bytes() {
            const buffer = await this.arrayBuffer();
            return new Uint8Array(buffer);
        }
    }
    global.Blob = Blob;
}

console.log('Running tests in Node.js environment...');
console.log('Node version:', process.version);
console.log('V8 version:', process.versions.v8);
console.log('');

// Set up mocha globals
const Mocha = require('mocha');
global.mocha = new Mocha();
global.describe = global.mocha.suite.describe = function() {};
global.it = global.mocha.suite.it = function() {};
global.before = global.mocha.suite.before = function() {};
global.after = global.mocha.suite.after = function() {};
global.beforeEach = global.mocha.suite.beforeEach = function() {};
global.afterEach = global.mocha.suite.afterEach = function() {};

// Load and run the compiled tests
try {
    require('../dist/tests.js');
} catch (err) {
    console.error('Test execution error:', err.message);
    console.error(err.stack);
    process.exit(1);
}