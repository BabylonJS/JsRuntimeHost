import * as Mocha from "mocha";
import { expect } from "chai";

Mocha.setup('bdd');
// @ts-ignore
Mocha.reporter('spec');

declare const hostPlatform: string;
declare const setExitCode: (code: number) => void;


describe("AbortController", function () {
    it("should not throw while aborting with no callbacks", function () {
        const controller = new AbortController();
        expect(controller.signal.aborted).to.equal(false);

        // Trigger with no callbacks
        controller.abort();

        expect(controller.signal.aborted).to.equal(true);
    });

    it("should not throw while aborting and correctly trigger both callbacks", function (done) {
        const controller = new AbortController();
        expect(controller.signal.aborted).to.equal(false);

        let cb1 = false, cb2 = false;

        // Expect aborted to be true after abort()
        controller.signal.onabort = () => {
            expect(controller.signal.aborted).to.equal(true);
            cb1 = true;

            if (cb1 && cb2) {
                done();
            }
        }

        controller.signal.addEventListener("abort", () => {
            expect(controller.signal.aborted).to.equal(true);
            cb2 = true;

            if (cb1 && cb2) {
                done();
            }
        })

        controller.abort();
    });

    it("should remove listener and not invoke callback function", function () {
        const controller = new AbortController();
        expect(controller.signal.aborted).to.equal(false);

        // If this function is unsuccessfully removed it will assert when called
        const onAbort = () => {
            expect(controller.signal.aborted).to.equal(false);
        };

        controller.signal.addEventListener("abort", onAbort);
        controller.signal.removeEventListener("abort", onAbort);

        controller.abort();

        expect(controller.signal.aborted).to.equal(true);
    });

    it("AbortSignal.abort() returns a signal already aborted with an AbortError reason", function () {
        const signal = (AbortSignal as any).abort();
        expect(signal.aborted).to.equal(true);
        expect(signal.reason).to.be.an.instanceof(Error);
        expect(signal.reason.name).to.equal("AbortError");
    });

    it("throwIfAborted() throws the reason only once aborted", function () {
        const controller = new AbortController();
        // Not aborted yet: must not throw.
        (controller.signal as any).throwIfAborted();

        controller.abort();
        expect(() => (controller.signal as any).throwIfAborted()).to.throw();
    });

    it("abort(reason) records the provided reason", function () {
        const controller = new AbortController();
        const reason = new Error("custom reason");
        controller.abort(reason);
        expect((controller.signal as any).reason).to.equal(reason);
    });

    it("abort() with no reason defaults to an AbortError", function () {
        const controller = new AbortController();
        controller.abort();
        const reason = (controller.signal as any).reason;
        expect(reason).to.be.an.instanceof(Error);
        expect(reason.name).to.equal("AbortError");
    });
});

describe("XMLHTTPRequest", function () {
    function createRequest(method: string, url: string, body: any = undefined, responseType: any = undefined): Promise<XMLHttpRequest> {
        return new Promise((resolve) => {
            const xhr = new XMLHttpRequest();
            xhr.open(method, url);
            if (responseType !== undefined) {
                xhr.responseType = responseType;
            }
            xhr.addEventListener("loadend", () => resolve(xhr));
            xhr.send(body);
        });
    }

    function createRequestWithHeaders(method: string, url: string, headers: any, body?: string): Promise<XMLHttpRequest> {
        return new Promise((resolve) => {
            const xhr = new XMLHttpRequest();
            xhr.open(method, url);
            headers.forEach((value: string, key: string) => xhr.setRequestHeader(key, value));
            xhr.addEventListener("loadend", () => resolve(xhr));
            xhr.send(body);
        });
    }

    this.timeout(0);

    it("should have readyState=4 when load ends", async function () {
        const xhr = await createRequest("GET", "https://github.com/");
        expect(xhr.readyState).to.equal(4);
    });

    it("should have status=200 for a file that exists", async function () {
        const xhr = await createRequest("GET", "https://github.com/");
        expect(xhr.status).to.equal(200);
    });

    it("should load URLs with escaped unicode characters", async function () {
        const xhr = await createRequest("GET", "https://raw.githubusercontent.com/BabylonJS/Assets/master/meshes/%CF%83%CF%84%CF%81%CE%BF%CE%B3%CE%B3%CF%85%CE%BB%CE%B5%CE%BC%CE%AD%CE%BD%CE%BF%CF%82%20%25%20%CE%BA%CF%8D%CE%B2%CE%BF%CF%82.glb");
        expect(xhr.status).to.equal(200);
    });

    it("should load URLs with unescaped unicode characters", async function () {
        const xhr = await createRequest("GET", "https://raw.githubusercontent.com/BabylonJS/Assets/master/meshes/στρογγυλεμένος%20%25%20κύβος.glb");
        expect(xhr.status).to.equal(200);
    });

    it("should load URLs with unescaped unicode characters and spaces", async function () {
        const xhr = await createRequest("GET", "https://raw.githubusercontent.com/BabylonJS/Assets/master/meshes/στρογγυλεμένος %25 κύβος.glb");
        expect(xhr.status).to.equal(200);
    });

    it("should have status=404 for a file that does not exist", async function () {
        const xhr = await createRequest("GET", "https://github.com/babylonJS/BabylonNative404");
        expect(xhr.status).to.equal(404);
    });

    it("should expose statusText", async function () {
        const okXhr = await createRequest("GET", "https://github.com/");
        expect(okXhr.statusText).to.equal("OK");
        const notFoundXhr = await createRequest("GET", "https://github.com/babylonJS/BabylonNative404");
        expect(notFoundXhr.statusText).to.equal("Not Found");
    });

    it("should fire 'error' event for a remote URL that returns HTTP 404", async function () {
        // Regression test: previously the success-only continuation in XMLHttpRequest::Send
        // skipped 'error' on async failures including non-2xx HTTP responses, so onerror
        // observers never ran. See https://github.com/BabylonJS/JsRuntimeHost/pull/165.
        this.timeout(30000);
        const result = await new Promise<{ errorFired: boolean; loadendFired: boolean; status: number; readyState: number }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            let errorFired = false;
            let loadendFired = false;
            const guard = setTimeout(() => reject(new Error("XHR neither errored nor loadended within 25s")), 25000);
            xhr.addEventListener("error", () => { errorFired = true; });
            xhr.addEventListener("loadend", () => {
                loadendFired = true;
                clearTimeout(guard);
                resolve({ errorFired, loadendFired, status: xhr.status, readyState: xhr.readyState });
            });
            xhr.open("GET", "https://github.com/babylonJS/BabylonNative404");
            xhr.send();
        });
        expect(result.status).to.equal(404);
        expect(result.errorFired).to.equal(true);
        expect(result.loadendFired).to.equal(true);
        expect(result.readyState).to.equal(4);
    });

    it("should expose errorCode/errorDetail diagnostics after a transport failure", async function () {
        this.timeout(30000);
        const xhr: any = await createRequest("GET", "http://127.0.0.1:1/");
        expect(xhr.status).to.equal(0);
        // Non-standard, additive diagnostics: always strings; populated on Apple/Linux and empty
        // on Windows/Android until those backends populate UrlLib's accessors. Either way the
        // standard error event + status===0 behavior (asserted above) is unchanged.
        expect(xhr.errorCode).to.be.a("string");
        expect(xhr.errorDetail).to.be.a("string");
    });

    it("should expose empty errorCode/errorDetail after a successful request", async function () {
        const xhr: any = await createRequest("GET", "app:///Scripts/symlink_target.js");
        expect(xhr.errorCode).to.equal("");
        expect(xhr.errorDetail).to.equal("");
    });

    it("should throw something when opening //", async function () {
        function openDoubleSlash() {
            const xhr = new XMLHttpRequest();
            xhr.open("GET", "//");
            xhr.send();
        }
        expect(openDoubleSlash).to.throw();
    });

    it("should throw something when opening a url with no scheme", function () {
        function openNoProtocol() {
            const xhr = new XMLHttpRequest();
            xhr.open("GET", "noscheme.glb");
            xhr.send();
        }
        expect(openNoProtocol).to.throw();
    });

    it("should throw something when sending before opening", function () {
        function sendWithoutOpening() {
            const xhr = new XMLHttpRequest();
            xhr.send();
        }
        expect(sendWithoutOpening).to.throw();
    });

    // TODO: httpbin server seems to be flaky right now. Re-enable these tests later.
    // if (hostPlatform !== "Unix") {
    //     it("should make a POST request with no body successfully", async function () {
    //         const xhr = await createRequest("POST", "https://httpbin.org/post");
    //         expect(xhr).to.have.property("readyState", 4);
    //         expect(xhr).to.have.property("status", 200);
    //     });

    //     it("should make a POST request with body successfully", async function () {
    //         const xhr = await createRequest("POST", "https://httpbin.org/post", "sampleBody");
    //         expect(xhr).to.have.property("readyState", 4);
    //         expect(xhr).to.have.property("status", 200);
    //     });
    // }

    // it("should make a GET request with headers successfully", async function () {
    //     const headersMap = new Map([["foo", "3"], ["bar", "3"]]);
    //     const xhr = await createRequestWithHeaders("GET", "https://httpbin.org/get", headersMap);
    //     expect(xhr).to.have.property("readyState", 4);
    //     expect(xhr).to.have.property("status", 200);
    // });

    // if (hostPlatform !== "Unix") {
    //     it("should make a POST request with body and headers successfully", async function () {
    //         const headersMap = new Map([["foo", "3"], ["bar", "3"]]);
    //         const xhr = await createRequestWithHeaders("POST", "https://httpbin.org/post", headersMap, "testBody");
    //         expect(xhr).to.have.property("readyState", 4);
    //         expect(xhr).to.have.property("status", 200);
    //     });
    // }

    if (hostPlatform === "macOS" || hostPlatform === "Unix" || hostPlatform === "Win32") {
        it("should load URL pointing to symlink", async function () {
            const xhr = await createRequest("GET", "app:///Scripts/symlink_1.js");
            expect(xhr).to.have.property("responseText", "var symlink_target_js = true;");
        });

        it("should load URL pointing to symlink that points to a symlink", async function () {
            const xhr = await createRequest("GET", "app:///Scripts/symlink_2.js");
            expect(xhr).to.have.property("responseText", "var symlink_target_js = true;");
        });
    }

    it("should load URL as array buffer", async function () {
        const xhr = await createRequest("GET", "app:///Scripts/symlink_target.js", undefined, "arraybuffer");
        var expected = new Uint8Array("var symlink_target_js = true;".split("").map(x => x.charCodeAt(0)));
        var response = new Uint8Array(xhr.response);
        expect(response).to.eql(expected);
    });

    it("should load a PLY file and parse vertex count from header using TextDecoder", async function () {
        this.timeout(30000);
        const xhr = await createRequest("GET", "app:///Assets/Halo_Believe.ply", undefined, "arraybuffer");
        expect(xhr.status).to.equal(200);

        const ubuf = new Uint8Array(xhr.response);
        const header = new TextDecoder().decode(ubuf.slice(0, 1024 * 10));
        const headerEnd = "end_header\n";
        const headerEndIndex = header.indexOf(headerEnd);
        expect(headerEndIndex).to.be.greaterThan(0);

        const vertexCount = parseInt(/element vertex (\d+)\n/.exec(header)![1]);
        expect(vertexCount).to.equal(18713);
    });
});

describe("Headers", function () {
    // Focused ports from WPT fetch/api/headers.
    it("normalizes names and values and combines repeated fields", function () {
        const headers = new Headers([
            ["X-Test", "  first\t"],
            ["x-test", "second"],
            ["X-Other", "\r\n value \n"]
        ]);
        expect(headers.get("X-TEST")).to.equal("first, second");
        expect(headers.get("x-other")).to.equal("value");
        expect(Array.from(headers.keys())).to.deep.equal(["x-other", "x-test"]);
    });

    it("validates sequence shape and HTTP ByteStrings", function () {
        expect(() => new Headers(null as any)).to.throw();
        expect(() => new Headers([["missing-value"]] as any)).to.throw();
        expect(() => new Headers([["too", "many", "values"]] as any)).to.throw();
        expect(() => new Headers([["invalid name", "value"]])).to.throw();
        expect(() => new Headers([["valid", "a\0b"]])).to.throw();
        expect(() => new Headers([["invalidĀ", "value"]])).to.throw();
    });

    it("preserves Set-Cookie fields while combining other duplicates", function () {
        const headers = new Headers([
            ["set-cookie", "a=1"],
            ["x-value", "first"],
            ["Set-Cookie", "b=2"],
            ["X-Value", "second"]
        ]);
        expect(headers.getSetCookie()).to.deep.equal(["a=1", "b=2"]);
        expect(Array.from(headers)).to.deep.equal([
            ["set-cookie", "a=1"],
            ["set-cookie", "b=2"],
            ["x-value", "first, second"]
        ]);
    });

    it("keeps iteration live when headers are changed", function () {
        const headers = new Headers({ bar: "0", baz: "1", foo: "2" });
        const seen: string[] = [];
        for (const [name] of headers) {
            seen.push(name);
            headers.delete("foo");
        }
        expect(seen).to.deep.equal(["bar", "baz"]);
    });

    it("copies an initializer and honors a custom iterator", function () {
        const source = new Headers({ ignored: "value" });
        source[Symbol.iterator] = function* () {
            yield ["custom", "value"];
        };
        const copy = new Headers(source);
        source.set("custom", "changed");
        expect(Array.from(copy)).to.deep.equal([["custom", "value"]]);
    });
});

describe("Response", function () {
    // Focused ports from WPT fetch/api/response.
    it("exposes browser defaults and validates response metadata", function () {
        const response = new Response();
        expect(String(response)).to.equal("[object Response]");
        expect(response.status).to.equal(200);
        expect(response.statusText).to.equal("");
        expect(response.ok).to.equal(true);
        expect(response.type).to.equal("default");
        expect(response.url).to.equal("");
        expect(response.body).to.equal(null);
        expect(response.headers).to.equal(response.headers);

        expect(() => new Response("", { status: 199 })).to.throw(RangeError);
        expect(() => new Response("", { status: 600 })).to.throw(RangeError);
        expect(() => new Response("", { statusText: "bad\ntext" })).to.throw(TypeError);
        expect(() => new Response("body", { status: 204 })).to.throw(TypeError);
    });

    it("streams strings and consumes a body only once", async function () {
        const response = new Response("streamed text");
        expect(response.body).to.be.instanceOf(ReadableStream);
        expect(response.bodyUsed).to.equal(false);
        expect(await response.text()).to.equal("streamed text");
        expect(response.bodyUsed).to.equal(true);

        let rejected = false;
        try {
            await response.arrayBuffer();
        } catch (error) {
            rejected = error instanceof TypeError;
        }
        expect(rejected).to.equal(true);
    });

    // Ported from WPT fetch/api/response/response-consume-empty.any.js.
    it("consumes a null body as empty without disturbing it", async function () {
        const textResponse = new Response();
        expect(await textResponse.text()).to.equal("");
        expect(textResponse.bodyUsed).to.equal(false);

        const bufferResponse = new Response();
        expect((await bufferResponse.arrayBuffer()).byteLength).to.equal(0);
        expect(bufferResponse.bodyUsed).to.equal(false);

        const bytesResponse = new Response();
        expect((await bytesResponse.bytes()).byteLength).to.equal(0);
        expect(bytesResponse.bodyUsed).to.equal(false);

        const blobResponse = new Response();
        expect((await blobResponse.blob()).size).to.equal(0);
        expect(blobResponse.bodyUsed).to.equal(false);

        const jsonResponse = new Response();
        let jsonRejected = false;
        try {
            await jsonResponse.json();
        } catch {
            jsonRejected = true;
        }
        expect(jsonRejected).to.equal(true);
        expect(jsonResponse.bodyUsed).to.equal(false);
    });

    // Byte streams reject zero-length enqueues. An empty BufferSource still
    // represents a non-null body, but its stream must close without a chunk.
    it("consumes an empty BufferSource without enqueueing an empty byte chunk", async function () {
        const response = new Response(new Uint8Array(0));
        expect(response.body).to.be.instanceOf(ReadableStream);
        expect(response.bodyUsed).to.equal(false);
        expect(new Uint8Array(await response.arrayBuffer())).to.eql(new Uint8Array(0));
        expect(response.bodyUsed).to.equal(true);
    });

    it("snapshots mutable BufferSource input once", async function () {
        const input = new Uint8Array([80, 65, 83, 83]);
        const response = new Response(input);
        input.fill(0);
        expect(await response.text()).to.equal("PASS");
    });

    // Adapted from WebKit LayoutTests/fetch/body-init.html.
    it("stringifies integer and object bodies", async function () {
        expect(await new Response(1 as any).text()).to.equal("1");
        expect(await new Response({} as any).text()).to.equal("[object Object]");
    });

    it("sets inferred content types without replacing explicit headers", async function () {
        const text = new Response("text");
        expect(text.headers.get("content-type")).to.equal("text/plain;charset=UTF-8");

        const blob = new Response(new Blob(["blob"], { type: "application/example" }));
        expect(blob.headers.get("content-type")).to.equal("application/example");

        const explicit = new Response("text", { headers: { "content-type": "text/custom" } });
        expect(explicit.headers.get("content-type")).to.equal("text/custom");
    });

    it("clones stream branches for independent consumption", async function () {
        const response = new Response("clone body", {
            headers: { "x-test": "value" },
            status: 201,
            statusText: "Created"
        });
        const clone = response.clone();
        expect(clone.status).to.equal(201);
        expect(clone.headers.get("x-test")).to.equal("value");
        expect(await response.text()).to.equal("clone body");
        expect(await clone.text()).to.equal("clone body");
    });

    function withoutPrivateDisturbedState(stream: ReadableStream<Uint8Array>): ReadableStream<Uint8Array> {
        Object.defineProperty(stream, "_disturbed", {
            configurable: true,
            get() { return undefined; },
            set() {}
        });
        return stream;
    }

    it("tracks direct stream reads and rejects invalid body chunks", async function () {
        const directStream = withoutPrivateDisturbedState(new ReadableStream({
            start(controller) {
                controller.enqueue(new TextEncoder().encode("body"));
                controller.close();
            }
        }));
        const direct = new Response(directStream);
        const reader = direct.body!.getReader();
        expect(direct.bodyUsed).to.equal(false);
        await reader.read();
        expect(direct.bodyUsed).to.equal(true);

        const invalid = new Response(new ReadableStream({
            start(controller) {
                controller.enqueue("not bytes");
                controller.close();
            }
        }) as any);
        let rejected = false;
        try {
            await invalid.bytes();
        } catch (error) {
            rejected = error instanceof TypeError;
        }
        expect(rejected).to.equal(true);
    });

    it("rejects a host-shaped stream disturbed before Response construction", async function () {
        const stream = withoutPrivateDisturbedState(new ReadableStream({
            start(controller) {
                controller.enqueue(new Uint8Array([1]));
                controller.close();
            }
        }));
        const reader = stream.getReader();
        await reader.read();
        reader.releaseLock();

        expect(() => new Response(stream)).to.throw(TypeError);
    });

    // Focused ports from WPT response-stream-disturbed-6.any.js and
    // response-stream-disturbed-by-pipe.any.js. These must not depend on a
    // private field supplied by one particular Streams implementation.
    it("tracks cancellation and piping through standard stream methods", async function () {
        const cancelled = new Response(withoutPrivateDisturbedState(new ReadableStream()));
        const cancelledReader = cancelled.body!.getReader();
        expect(cancelled.bodyUsed).to.equal(false);
        await cancelledReader.cancel();
        expect(cancelled.bodyUsed).to.equal(true);

        const piped = new Response(withoutPrivateDisturbedState(new ReadableStream({
            start(controller) {
                controller.close();
            }
        })));
        const pipePromise = piped.body!.pipeTo(new WritableStream({}, { highWaterMark: 0 }));
        expect(piped.bodyUsed).to.equal(true);
        await pipePromise;

        const pipedThrough = new Response(withoutPrivateDisturbedState(new ReadableStream({
            start(controller) {
                controller.close();
            }
        })));
        const output = pipedThrough.body!.pipeThrough(new TransformStream());
        expect(pipedThrough.bodyUsed).to.equal(true);
        await output.cancel();
    });

    // Adapted from Firefox dom/fetch/tests/crashtests/1939295.html. The
    // unresolved read must remain safe through runtime teardown.
    it("does not crash while consuming an open empty stream", function () {
        const pending = new Response(new ReadableStream()).text();
        expect(pending).to.be.instanceOf(Promise);
    });

    // Adapted from WebKit's imported many-empty-chunks-crash.html.
    it("consumes many empty chunks without retaining growing byte buffers", async function () {
        const response = new Response(new ReadableStream({
            start(controller) {
                for (let index = 0; index < 40000; ++index) {
                    controller.enqueue(new Uint8Array());
                }
                controller.close();
            }
        }));
        expect((await response.arrayBuffer()).byteLength).to.equal(0);
    });

    // Bounded adaptation of Chromium's call-extra-crash-is-disturbed.html.
    // QuickJS terminates before a JS-defined getter can run after a real
    // native stack overflow, so retain the deep-call regression portably.
    it("reads bodyUsed from a deep call stack without recursion", function () {
        const response = new Response(new ReadableStream());
        function readAtDepth(depth: number): boolean {
            return depth === 0 ? response.bodyUsed : readAtDepth(depth - 1);
        }
        expect(readAtDepth(128)).to.equal(false);
    });

    it("provides error, redirect, and JSON factories", async function () {
        const error = Response.error();
        expect(error.type).to.equal("error");
        expect(error.status).to.equal(0);

        const redirect = Response.redirect("https://example.com/path", 307);
        expect(redirect.status).to.equal(307);
        expect(redirect.headers.get("location")).to.equal("https://example.com/path");

        const json = Response.json({ value: 42 });
        expect(json.headers.get("content-type")).to.equal("application/json");
        expect(await json.json()).to.deep.equal({ value: 42 });
    });

    it("filters forbidden response Set-Cookie fields", function () {
        const response = new Response(null, {
            headers: {
                "set-cookie": "secret=value",
                "set-cookie2": "legacy=value"
            }
        });
        response.headers.append("Set-Cookie", "other=value");
        expect(response.headers.getSetCookie()).to.deep.equal([]);
        expect(response.headers.has("set-cookie2")).to.equal(false);
    });
});

describe("fetch", function () {
    this.timeout(30000);

    it("should resolve with ok=true and status=200 for a resource that exists", async function () {
        const response = await fetch("https://github.com/");
        expect(response).to.be.instanceOf(Response);
        expect(response.headers).to.be.instanceOf(Headers);
        expect(response.body).to.be.instanceOf(ReadableStream);
        expect(response.ok).to.equal(true);
        expect(response.status).to.equal(200);
    });

    it("should resolve (not reject) with ok=false and status=404 for a resource that does not exist", async function () {
        const response = await fetch("https://github.com/babylonJS/BabylonNative404");
        expect(response.ok).to.equal(false);
        expect(response.status).to.equal(404);
    });

    it("should expose statusText", async function () {
        const okResponse = await fetch("https://github.com/");
        expect(okResponse.statusText).to.equal("OK");
        const notFoundResponse = await fetch("https://github.com/babylonJS/BabylonNative404");
        expect(notFoundResponse.statusText).to.equal("Not Found");
    });

    it("text() should return the body as a string", async function () {
        const response = await fetch("app:///Scripts/symlink_target.js");
        expect(await response.text()).to.equal("var symlink_target_js = true;");
    });

    it("arrayBuffer() should return the body as bytes", async function () {
        const response = await fetch("app:///Scripts/symlink_target.js");
        const expected = new Uint8Array("var symlink_target_js = true;".split("").map(x => x.charCodeAt(0)));
        expect(new Uint8Array(await response.arrayBuffer())).to.eql(expected);
    });

    it("json() should parse a JSON body", async function () {
        const response = await fetch("app:///Assets/sample.json");
        const json = await response.json();
        expect(json.name).to.equal("fetch-polyfill-test");
        expect(json.value).to.equal(42);
        expect(json.nested.items).to.eql([1, 2, 3]);
    });

    it("json() should reject when the body is not valid JSON", async function () {
        const response = await fetch("app:///Scripts/symlink_target.js");
        let rejected = false;
        try {
            await response.json();
        } catch {
            rejected = true;
        }
        expect(rejected).to.equal(true);
    });

    it("blob() should return a Blob with the body bytes", async function () {
        const response = await fetch("app:///Scripts/symlink_target.js");
        const blob = await response.blob();
        expect(blob.size).to.equal("var symlink_target_js = true;".length);
        expect(await blob.text()).to.equal("var symlink_target_js = true;");
    });

    it("headers.get() should be case-insensitive and headers.has() should work", async function () {
        const response = await fetch("https://github.com/");
        expect(response.headers.has("Content-Type")).to.equal(true);
        expect(response.headers.get("CONTENT-TYPE")).to.equal(response.headers.get("content-type"));
    });

    it("clone() should produce an independently readable response", async function () {
        const response = await fetch("app:///Scripts/symlink_target.js");
        const clone = response.clone();
        expect(await response.text()).to.equal("var symlink_target_js = true;");
        expect(await clone.text()).to.equal("var symlink_target_js = true;");
    });

    it("should accept a method in the init object", async function () {
        const response = await fetch("https://github.com/", { method: "GET" });
        expect(response.status).to.equal(200);
    });

    it("should reject when no arguments are provided", async function () {
        let rejected = false;
        try {
            await (fetch as any)();
        } catch {
            rejected = true;
        }
        expect(rejected).to.equal(true);
    });

    it("should reject a transport failure with a TypeError carrying detail on cause", async function () {
        this.timeout(30000);
        let error: any;
        try {
            // Nothing listens on this loopback port, so the connection is refused -- a transport
            // failure (status 0), distinct from an HTTP error status.
            await fetch("http://127.0.0.1:1/");
        } catch (e) {
            error = e;
        }
        expect(error, "fetch should have rejected").to.not.equal(undefined);
        // Spec-conformant shape: network errors reject with a TypeError whose message is stable
        // (browsers/Node/undici all keep it constant so crash-report grouping stays intact).
        expect(error).to.be.an.instanceof(TypeError);
        expect(error.message).to.equal("fetch failed");
        // The variable detail rides on `cause` (Node/undici shape), never on the stable message.
        expect(error.cause, "error.cause should be populated").to.be.an("object");
        expect(error.cause.url).to.contain("127.0.0.1");
        expect(error.cause.status).to.equal(0);
        // On backends where UrlLib populates transport detail (Apple/Linux) `code`/`detail` are
        // present stable tokens; on backends that don't yet (Windows/Android) they are absent --
        // the stable observable shape above is preserved either way.
        if (error.cause.code !== undefined) {
            expect(error.cause.code).to.be.a("string").and.not.equal("");
            expect(error.cause.detail).to.be.a("string").and.not.equal("");
        }
    });

    it("should reject a missing app:// asset with a TypeError (distinct from a network failure)", async function () {
        let error: any;
        try {
            await fetch("app:///does_not_exist.js");
        } catch (e) {
            error = e;
        }
        expect(error, "fetch should have rejected").to.not.equal(undefined);
        expect(error).to.be.an.instanceof(TypeError);
        expect(error.message).to.equal("fetch failed");
        expect(error.cause.url).to.contain("does_not_exist.js");
    });

    it("should reject immediately with an AbortError when the signal is already aborted", async function () {
        const controller = new AbortController();
        controller.abort();

        let error: any;
        try {
            await fetch("https://github.com/", { signal: controller.signal } as any);
        } catch (e) {
            error = e;
        }
        expect(error, "fetch should have rejected").to.not.equal(undefined);
        expect(error.name).to.equal("AbortError");
    });

    it("should reject with an AbortError when aborted in-flight", async function () {
        this.timeout(30000);
        const controller = new AbortController();
        const promise = fetch("https://github.com/", { signal: controller.signal } as any);
        // Abort before the response can arrive.
        controller.abort();

        let error: any;
        try {
            await promise;
        } catch (e) {
            error = e;
        }
        expect(error, "fetch should have rejected").to.not.equal(undefined);
        expect(error.name).to.equal("AbortError");
    });
});

describe("setTimeout", function () {
    this.timeout(5000);

    it("should return an id greater than zero", function () {
        const id = setTimeout(() => { }, 0);
        expect(id).to.be.greaterThan(0);
    });

    it("should return an id greater than zero when given an undefined function", function () {
        const id = setTimeout(undefined as any, 0);
        expect(id).to.be.greaterThan(0);
    });

    it("should call the given function after the given delay", function (done) {
        const startTime = new Date().getTime();
        setTimeout(() => {
            try {
                expect(new Date().getTime() - startTime).to.be.at.least(10);
                done();
            }
            catch (e) {
                done(e);
            }
        }, 10);
    });

    it("should call the given nested function after the given delay", function (done) {
        const startTime = new Date().getTime();
        setTimeout(() => {
            setTimeout(() => {
                try {
                    expect(new Date().getTime() - startTime).to.be.at.least(20);
                    done();
                }
                catch (e) {
                    done(e);
                }
            }, 10);
        }, 10);
    });

    it("should call the given function after the given delay when the delay is a string representing a valid number", function (done) {
        const startTime = new Date().getTime();
        setTimeout(() => {
            try {
                expect(new Date().getTime() - startTime).to.be.at.least(10);
                done();
            }
            catch (e) {
                done(e);
            }
        }, "10" as any);
    });

    it("should call the given function after zero milliseconds when the delay is a string representing an invalid number", function (done) {
        setTimeout(() => {
            done();
        }, "a" as any);
    });

    it("should call the given function after other tasks execute when the given delay is zero", function (done) {
        let trailingCodeExecuted = false;
        setTimeout(() => {
            try {
                expect(trailingCodeExecuted).to.be.true;
                done();
            }
            catch (e) {
                done(e);
            }
        }, 0);
        trailingCodeExecuted = true;
    });

    it("should call the given function after other tasks execute when the given delay is undefined", function (done) {
        let trailingCodeExecuted = false;
        setTimeout(() => {
            try {
                expect(trailingCodeExecuted).to.be.true;
                done();
            }
            catch (e) {
                done(e);
            }
        }, undefined);
        trailingCodeExecuted = true;
    });

    // See https://github.com/BabylonJS/JsRuntimeHost/issues/9
    // it("should call the given functions in the correct order", function (done) {
    //     const called = [];
    //     for (let i = 9; i >= 0; i--) {
    //         setTimeout(() => {
    //             called.push(i);
    //             if (called.length === 10) {
    //                 try {
    //                     expect(called).to.deep.equal([0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
    //                     done();
    //                 }
    //                 catch (e) {
    //                     done(e);
    //                 }
    //             }
    //         }, i * 10);
    //     }
    // });
});

describe("clearTimeout", function () {
    this.timeout(5000);

    it("should stop the timeout matching the given timeout id", function (done) {
        const id = setTimeout(() => {
            done(new Error("Timeout was not cleared"));
        }, 0);
        clearTimeout(id);
        setTimeout(done, 100);
    });

    it("should do nothing if the given timeout id is undefined", function (done) {
        setTimeout(() => { done(); }, 0);
        clearTimeout(undefined);
    });

    it("should be interchangeable with clearInterval", function (done) {
        const id = setTimeout(() => {
            done(new Error("Interval was not cleared"));
        }, 0);
        clearInterval(id);
        setTimeout(done, 100);
    });
});

describe("setInterval", function () {
    this.timeout(5000);

    it("should return an id greater than zero", function () {
        const id = setInterval(() => { }, 0);
        clearInterval(id);
        expect(id).to.be.greaterThan(0);
    });

    it("should call the given function at the given interval", function (done) {
        let startTime = new Date().getTime();
        let tickCount = 0;
        const id = setInterval(() => {
            try {
                tickCount++;
                expect(new Date().getTime() - startTime).to.be.at.least(tickCount * 10);
                if (tickCount > 2) {
                    clearInterval(id);
                    done();
                }
            }
            catch (e) {
                console.log(`finished with error: ${e}`);
                clearInterval(id);
                done(e);
            }
        }, 10);
    });
});

describe("clearInterval", function () {
    this.timeout(5000);

    it("should stop the interval matching the given interval id", function (done) {
        const id = setInterval(() => {
            done(new Error("Interval was not cleared"));
        }, 0);
        clearInterval(id);
        setTimeout(done, 100);
    });

    it("should do nothing if the given interval id is undefined", function (done) {
        setTimeout(() => { done(); }, 0);
        clearInterval(undefined);
    });

    it("should be interchangeable with clearTimeout", function (done) {
        const id = setInterval(() => {
            done(new Error("Interval was not cleared"));
        }, 0);
        clearTimeout(id);
        setTimeout(done, 100);
    });
});

// Websocket
if (hostPlatform !== "Unix") {
    describe("WebSocket", function () {
        this.timeout(10000);

        it("should connect correctly with one websocket connection", function (done) {
            const ws = new WebSocket("wss://ws.postman-echo.com/raw");
            const testMessage = "testMessage";
            let error: unknown;

            ws.onopen = () => {
                try {
                    expect(ws).to.have.property("readyState", 1);
                    expect(ws).to.have.property("url", "wss://ws.postman-echo.com/raw");
                    ws.send(testMessage);
                }
                catch (e) {
                    error = e;
                    ws.close();
                }
            };

            ws.onmessage = (msg) => {
                try {
                    expect(msg.data).to.equal(testMessage);
                }
                catch (e) {
                    error = e;
                }
                ws.close();
            };

            ws.onclose = () => {
                if (!error) {
                    try {
                        expect(ws).to.have.property("readyState", 3);
                    }
                    catch (e) {
                        error = e;
                    }
                }
                done(error);
            };

            ws.onerror = () => {
                error = new Error("WebSocket failed");
            };
        });

        it("should connect correctly with multiple websocket connections", function (done) {
            this.timeout(10000);
            const testMessage1 = "testMessage1";
            const testMessage2 = "testMessage2";
            let error: unknown;

            const ws1 = new WebSocket("wss://ws.postman-echo.com/raw");
            ws1.onopen = () => {
                const ws2 = new WebSocket("wss://ws.postman-echo.com/raw");
                ws2.onopen = () => {
                    try {
                        expect(ws2).to.have.property("readyState", 1);
                        expect(ws2).to.have.property("url", "wss://ws.postman-echo.com/raw");
                        ws2.send(testMessage2);
                    }
                    catch (e) {
                        error = e;
                        ws2.close();
                    }
                };

                ws2.onmessage = (msg) => {
                    try {
                        expect(msg.data).to.equal(testMessage2);
                    }
                    catch (e) {
                        error = e;
                    }
                    ws2.close();
                };

                ws2.onclose = () => {
                    if (!error) {
                        try {
                            expect(ws2).to.have.property("readyState", 3);
                            ws1.send(testMessage1);
                        }
                        catch (e) {
                            error = e;
                            ws1.close();
                        }
                    }
                    else {
                        ws1.close();
                    }
                };

                ws2.onerror = () => {
                    error = new Error("WebSocket failed");
                };
            }

            ws1.onmessage = (msg) => {
                try {
                    expect(msg.data).to.equal(testMessage1);
                }
                catch (e) {
                    error = e;
                }
                ws1.close();
            }

            ws1.onclose = () => {
                if (!error) {
                    try {
                        expect(ws1).to.have.property("readyState", 3);
                    }
                    catch (e) {
                        error = e;
                    }
                }
                done(error);
            }

            ws1.onerror = () => {
                error = new Error("WebSocket failed");
            };
        });

        it("should trigger error callback with invalid server", function (done) {
            this.timeout(10000);
            // Random UUID-based hostname so the domain is guaranteed unregistered
            // (RFC-reserved `.invalid` causes a >10s DNS path on Win32 x86 Chakra).
            const ws = new WebSocket("wss://caddddfd-ee88-4771-b293-8a8e13b330ee.com");
            let errorFired = false;
            ws.onerror = () => {
                errorFired = true;
            };
            ws.onclose = () => {
                try {
                    expect(errorFired).to.be.true;
                    done();
                }
                catch (e) {
                    done(e);
                }
            };
        });

        it("should trigger error callback with invalid domain", function (done) {
            this.timeout(10000);
            const ws = new WebSocket("wss://example");
            let errorFired = false;
            ws.onerror = () => {
                errorFired = true;
            };
            ws.onclose = () => {
                try {
                    expect(errorFired).to.be.true;
                    done();
                }
                catch (e) {
                    done(e);
                }
            };
        });
    })
}

// URL
describe("URL", function () {

    // Currently all of the properties that the polyfill has implemented
    interface URLCheckOptions {
        href: string;
        hostname: string;
        origin: string;
        pathname: string;
        search: string;
    }

    function checkURL(url: URL, { href, hostname, origin, pathname, search }: URLCheckOptions): void {
        expect(url).to.have.property("hostname", hostname);
        expect(url).to.have.property("href", href);
        expect(url).to.have.property("origin", origin);
        expect(url).to.have.property("pathname", pathname);
        expect(url).to.have.property("search", search);
    }

    it("should load URL with no pathname / search", function () {
        // Standard URL (No pathname, no search)
        const url = new URL("https://httpbin.org");
        checkURL(url, {
            href: "https://httpbin.org/",
            hostname: "httpbin.org",
            origin: "https://httpbin.org",
            pathname: "/",
            search: ""
        });
    });

    it("should load URL with pathname (no search)", function () {
        // Augment URL with pathname (no search)
        const url = new URL("https://httpbin.org/en-US/docs");
        checkURL(url, {
            href: "https://httpbin.org/en-US/docs",
            hostname: "httpbin.org",
            origin: "https://httpbin.org",
            pathname: "/en-US/docs",
            search: ""
        });
    });

    it("should load URL with pathname and search", function () {
        // Augment URL with pathname and search
        const url = new URL("https://httpbin.org/en-US/docs?foo=1&bar=2");
        checkURL(url, {
            href: "https://httpbin.org/en-US/docs?foo=1&bar=2",
            hostname: "httpbin.org",
            origin: "https://httpbin.org",
            pathname: "/en-US/docs",
            search: "?foo=1&bar=2"
        });
    });

    it("should load URL with pathname and search with multiple key value pairs", function () {
        const url = new URL("https://httpbin.org/en-US/docs?c=3&b=2&a=1&d=4");
        checkURL(url, {
            href: "https://httpbin.org/en-US/docs?c=3&b=2&a=1&d=4",
            hostname: "httpbin.org",
            origin: "https://httpbin.org",
            pathname: "/en-US/docs",
            search: "?c=3&b=2&a=1&d=4"
        });
    });

    it("should update href after URLSearchParams are changed", function () {
        // Augment URL with pathname and search
        const url = new URL("https://httpbin.org/en-US/docs?foo=1&bar=2");
        url.searchParams.set("foo", 999 as any);
        // href should change to reflect searchParams change
        checkURL(url, {
            href: "https://httpbin.org/en-US/docs?foo=999&bar=2",
            hostname: "httpbin.org",
            origin: "https://httpbin.org",
            pathname: "/en-US/docs",
            search: "?foo=999&bar=2"
        });
    });

    it("should update href after URLSearchParams are changed (Starting with 0 params)", function () {
        // Augment URL with pathname and search
        const url = new URL("https://httpbin.org/en-US/docs");
        url.searchParams.set("foo", "999");
        // href should change to reflect searchParams change
        checkURL(url, {
            href: "https://httpbin.org/en-US/docs?foo=999",
            hostname: "httpbin.org",
            origin: "https://httpbin.org",
            pathname: "/en-US/docs",
            search: "?foo=999"
        });
    });

    // -------------------------------- URL Properties --------------------------------

    it("should parse protocol correctly", function () {
        const url = new URL("https://example.com/path");
        expect(url.protocol).to.equal("https:");
    });

    it("should parse port correctly", function () {
        const url = new URL("https://example.com:8080/path");
        expect(url.port).to.equal("8080");
        expect(url.host).to.equal("example.com:8080");
    });

    it("should return empty port when not specified", function () {
        const url = new URL("https://example.com/path");
        expect(url.port).to.equal("");
        expect(url.host).to.equal("example.com");
    });

    it("should parse hash correctly", function () {
        const url = new URL("https://example.com/path#section1");
        expect(url.hash).to.equal("#section1");
    });

    it("should return empty hash when not present", function () {
        const url = new URL("https://example.com/path");
        expect(url.hash).to.equal("");
    });

    it("should parse username and password correctly", function () {
        const url = new URL("https://user:pass@example.com/path");
        expect(url.username).to.equal("user");
        expect(url.password).to.equal("pass");
    });

    it("should return empty username and password when not present", function () {
        const url = new URL("https://example.com/path");
        expect(url.username).to.equal("");
        expect(url.password).to.equal("");
    });

    it("should parse URL with all components", function () {
        const url = new URL("https://user:pass@example.com:8080/path/to/resource?foo=1&bar=2#section");
        expect(url.protocol).to.equal("https:");
        expect(url.username).to.equal("user");
        expect(url.password).to.equal("pass");
        expect(url.hostname).to.equal("example.com");
        expect(url.port).to.equal("8080");
        expect(url.host).to.equal("example.com:8080");
        expect(url.pathname).to.equal("/path/to/resource");
        expect(url.search).to.equal("?foo=1&bar=2");
        expect(url.hash).to.equal("#section");
        expect(url.origin).to.equal("https://example.com:8080");
    });

    // -------------------------------- URL Property Setters --------------------------------

    it("should update protocol via setter", function () {
        const url = new URL("https://example.com/path");
        url.protocol = "http:";
        expect(url.protocol).to.equal("http:");
        expect(url.href).to.equal("http://example.com/path");
    });

    it("should add colon to protocol if missing", function () {
        const url = new URL("https://example.com/path");
        url.protocol = "http";
        expect(url.protocol).to.equal("http:");
    });

    it("should update hostname via setter", function () {
        const url = new URL("https://example.com/path");
        url.hostname = "newhost.com";
        expect(url.hostname).to.equal("newhost.com");
        expect(url.href).to.equal("https://newhost.com/path");
    });

    it("should update host via setter (hostname only)", function () {
        const url = new URL("https://example.com:8080/path");
        url.host = "newhost.com";
        expect(url.hostname).to.equal("newhost.com");
        expect(url.port).to.equal("");
        expect(url.host).to.equal("newhost.com");
    });

    it("should update host via setter (hostname and port)", function () {
        const url = new URL("https://example.com/path");
        url.host = "newhost.com:9090";
        expect(url.hostname).to.equal("newhost.com");
        expect(url.port).to.equal("9090");
        expect(url.host).to.equal("newhost.com:9090");
    });

    it("should update port via setter", function () {
        const url = new URL("https://example.com/path");
        url.port = "3000";
        expect(url.port).to.equal("3000");
        expect(url.href).to.equal("https://example.com:3000/path");
    });

    it("should update pathname via setter", function () {
        const url = new URL("https://example.com/path");
        url.pathname = "/new/path";
        expect(url.pathname).to.equal("/new/path");
        expect(url.href).to.equal("https://example.com/new/path");
    });

    it("should add leading slash to pathname if missing", function () {
        const url = new URL("https://example.com/path");
        url.pathname = "new/path";
        expect(url.pathname).to.equal("/new/path");
    });

    it("should update search via setter", function () {
        const url = new URL("https://example.com/path");
        url.search = "?newkey=newvalue";
        expect(url.search).to.equal("?newkey=newvalue");
        expect(url.href).to.equal("https://example.com/path?newkey=newvalue");
    });

    it("should update search via setter without leading question mark", function () {
        const url = new URL("https://example.com/path");
        url.search = "key=value";
        expect(url.search).to.equal("?key=value");
    });

    it("should update hash via setter", function () {
        const url = new URL("https://example.com/path");
        url.hash = "#newsection";
        expect(url.hash).to.equal("#newsection");
        expect(url.href).to.equal("https://example.com/path#newsection");
    });

    it("should add leading hash if missing", function () {
        const url = new URL("https://example.com/path");
        url.hash = "newsection";
        expect(url.hash).to.equal("#newsection");
    });

    it("should update username via setter", function () {
        const url = new URL("https://example.com/path");
        url.username = "newuser";
        expect(url.username).to.equal("newuser");
        expect(url.href).to.equal("https://newuser@example.com/path");
    });

    it("should update password via setter", function () {
        const url = new URL("https://user@example.com/path");
        url.password = "newpass";
        expect(url.password).to.equal("newpass");
        expect(url.href).to.equal("https://user:newpass@example.com/path");
    });

    it("should update href via setter", function () {
        const url = new URL("https://example.com/path");
        url.href = "https://newsite.com/newpath?q=1#hash";
        expect(url.hostname).to.equal("newsite.com");
        expect(url.pathname).to.equal("/newpath");
        expect(url.search).to.equal("?q=1");
        expect(url.hash).to.equal("#hash");
    });

    // -------------------------------- URL Instance Methods --------------------------------

    it("should return href from toString()", function () {
        const url = new URL("https://example.com/path?foo=1#section");
        expect(url.toString()).to.equal("https://example.com/path?foo=1#section");
    });

    it("should return href from toJSON()", function () {
        const url = new URL("https://example.com/path?foo=1#section");
        expect(url.toJSON()).to.equal("https://example.com/path?foo=1#section");
    });

    // -------------------------------- URL Static Methods --------------------------------

    it("should return true from canParse() for valid absolute URL", function () {
        expect(URL.canParse("https://example.com")).to.equal(true);
    });

    it("should return false from canParse() for invalid URL", function () {
        expect(URL.canParse("not-a-url")).to.equal(false);
    });

    it("should return true from canParse() for relative URL with valid base", function () {
        expect(URL.canParse("/path", "https://example.com")).to.equal(true);
    });

    it("should return false from canParse() for relative URL with invalid base", function () {
        expect(URL.canParse("/path", "not-a-url")).to.equal(false);
    });

    it("should return false from canParse() with no arguments", function () {
        // @ts-expect-error - testing no arguments
        expect(URL.canParse()).to.equal(false);
    });

    it("should return URL object from parse() for valid URL", function () {
        const url = URL.parse("https://example.com/path");
        expect(url).to.not.equal(null);
        expect(url!.hostname).to.equal("example.com");
        expect(url!.pathname).to.equal("/path");
    });

    // TODO: Uncomment this once the the Node-API implementation for Chakra supports throwing errors from constructors.
    // it("should return null from parse() for invalid URL", function () {
    //     const url = URL.parse("not-a-url");
    //     expect(url).to.equal(null);
    // });

    it("should return URL object from parse() with valid base", function () {
        const url = URL.parse("/path", "https://example.com");
        expect(url).to.not.equal(null);
        expect(url!.hostname).to.equal("example.com");
        expect(url!.pathname).to.equal("/path");
    });

    // TODO: Uncomment this once the the Node-API implementation for Chakra supports throwing errors from constructors.
    // it("should return null from parse() with invalid base", function () {
    //     const url = URL.parse("/path", "not-a-url");
    //     expect(url).to.equal(null);
    // });

    // -------------------------------- URL Base URL Resolution --------------------------------

    it("should resolve relative URL with base URL", function () {
        const url = new URL("/path/to/resource", "https://example.com");
        expect(url.href).to.equal("https://example.com/path/to/resource");
    });

    it("should resolve relative path against base URL", function () {
        const url = new URL("resource.html", "https://example.com/path/to/");
        expect(url.href).to.equal("https://example.com/path/to/resource.html");
    });

    it("should resolve relative path against base URL with file", function () {
        const url = new URL("resource.html", "https://example.com/path/to/index.html");
        expect(url.href).to.equal("https://example.com/path/to/resource.html");
    });

    it("should resolve query-only relative URL", function () {
        const url = new URL("?newquery=1", "https://example.com/path");
        expect(url.href).to.equal("https://example.com/path?newquery=1");
    });

    it("should resolve hash-only relative URL", function () {
        const url = new URL("#newhash", "https://example.com/path?query=1");
        expect(url.href).to.equal("https://example.com/path?query=1#newhash");
    });

    it("should resolve protocol-relative URL", function () {
        const url = new URL("//other.com/path", "https://example.com");
        expect(url.href).to.equal("https://other.com/path");
    });

    it("should use absolute URL and ignore base when URL is absolute", function () {
        const url = new URL("https://other.com/otherpath", "https://example.com/path");
        expect(url.href).to.equal("https://other.com/otherpath");
    });

    it("should resolve dot segments in path (single dot)", function () {
        const url = new URL("https://example.com/a/b/./c");
        expect(url.pathname).to.equal("/a/b/c");
    });

    it("should resolve dot segments in path (double dot)", function () {
        const url = new URL("https://example.com/a/b/../c");
        expect(url.pathname).to.equal("/a/c");
    });

    it("should resolve complex dot segments", function () {
        const url = new URL("https://example.com/a/b/c/../d/./e/../f");
        expect(url.pathname).to.equal("/a/b/d/f");
    });

    it("should resolve relative path with dot segments", function () {
        const url = new URL("../sibling/file.html", "https://example.com/path/to/current/");
        expect(url.pathname).to.equal("/path/to/sibling/file.html");
    });

    // -------------------------------- URL Error Handling --------------------------------

    // TODO: Uncomment this once the the Node-API implementation for Chakra supports throwing errors from constructors.
    // it("should throw for invalid URL without base", function () {
    //     expect(() => new URL("not-a-valid-url")).to.throw();
    // });

    // TODO: Uncomment this once the the Node-API implementation for Chakra supports throwing errors from constructors.
    // it("should throw for relative URL without base", function () {
    //     expect(() => new URL("/path/to/resource")).to.throw();
    // });

    // TODO: Uncomment this once the the Node-API implementation for Chakra supports throwing errors from constructors.
    // it("should throw for empty URL constructor", function () {
    //     // @ts-expect-error - testing no arguments
    //     expect(() => new URL()).to.throw();
    // });

    // -------------------------------- URL Different Schemes --------------------------------

    it("should parse file:// URL", function () {
        const url = new URL("file:///path/to/file.txt");
        expect(url.protocol).to.equal("file:");
        expect(url.pathname).to.equal("/path/to/file.txt");
    });

    it("should parse ftp:// URL", function () {
        const url = new URL("ftp://ftp.example.com/file.zip");
        expect(url.protocol).to.equal("ftp:");
        expect(url.hostname).to.equal("ftp.example.com");
    });

    it("should parse ws:// URL", function () {
        const url = new URL("ws://example.com/socket");
        expect(url.protocol).to.equal("ws:");
        expect(url.hostname).to.equal("example.com");
    });

    it("should parse wss:// URL", function () {
        const url = new URL("wss://example.com/socket");
        expect(url.protocol).to.equal("wss:");
        expect(url.hostname).to.equal("example.com");
    });

    it("should parse custom scheme URL", function () {
        const url = new URL("app:///Scripts/test.js");
        expect(url.protocol).to.equal("app:");
        expect(url.pathname).to.equal("/Scripts/test.js");
    });
});

// URLSearchParams
describe("URLSearchParams", function () {

    // -------------------------------- URLSearchParams Get --------------------------------

    it("should retrieve null from empty searchParams", function () {
        // Get Empty
        const params = new URLSearchParams("");

        expect(params.get("foo")).to.equal(null);
    });

    it("should retrieve value from searchParams", function () {
        // Get Value
        const params = new URLSearchParams("?foo=1");

        expect(params.get("foo")).to.equal("1");
    });

    // -------------------------------- URLSearchParams Set --------------------------------

    const paramsSet = new URLSearchParams("");

    it("should throw exception when trying to set with less than 2 parameters", function () {
        // `set` expects parameters, none given.
        // @ts-expect-error
        expect(() => paramsSet.set()).to.throw();
    });

    it("should add a number and retrieve it as a string from searchParams", function () {
        // Set Number
        paramsSet.set("foo", 400 as any);
        expect(paramsSet.get("foo")).to.equal("400");
    });

    it("should add a string and retrieve it as a string from searchParams", function () {
        // Set String
        paramsSet.set("bar", "50");
        expect(paramsSet.get("bar")).to.equal("50");
    });

    it("should add a boolean and retrieve it as a string from searchParams", function () {
        // Set Boolean
        paramsSet.set("baz", true as any);
        expect(paramsSet.get("baz")).to.equal("true");
    });

    it("should set an existing number and retrieve it as a string from searchParams", function () {
        // Set Existing Value
        paramsSet.set("foo", 9999 as any);
        expect(paramsSet.get("foo")).to.equal("9999");
    });

    // -------------------------------- URLSearchParams Has --------------------------------

    it("should check value is in searchParams (True)", function () {
        // Check existing value
        const paramsHas = new URLSearchParams("?foo=1");
        expect(paramsHas.has("foo")).to.equal(true);
    });

    it("should check value is in searchParams (False)", function () {
        // Check non-existing value
        const paramsHas = new URLSearchParams("?foo=1");
        expect(paramsHas.has("Microsoft")).to.equal(false);
    });

    it("should check empty searchParams for value (False)", function () {
        // Check empty params
        const paramsEmpty = new URLSearchParams("");
        expect(paramsEmpty.has("foo")).to.equal(false);
    });

    // -------------------------------- URLSearchParams Construction --------------------------------

    it("should retrieve search params set at construction", function () {
        // Retrieve params via url.search, passed into ctor
        const url = new URL("https://example.com?foo=1&bar=2");
        const params1 = new URLSearchParams(url.search);
        expect(params1.get("foo")).to.equal("1");
        expect(params1.get("bar")).to.equal("2");
    });

    it("should retrieve search params from url.searchParams object", function () {
        // Get the URLSearchParams object directly from an URL object
        const url = new URL("https://example.com?foo=1&bar=2");
        const params1a = url.searchParams;
        expect(params1a.get("foo")).to.equal("1");
        expect(params1a.get("bar")).to.equal("2");
    });

    it("should retrieve search params string constructed URLSearchParams", function () {
        // Pass in a string literal
        const params2 = new URLSearchParams("foo=1&bar=2");
        expect(params2.get("foo")).to.equal("1");
        expect(params2.get("bar")).to.equal("2");
    });
});

describe("Console", function () {
    it("should log a simple console log string without error", function () {
        expect(() => console.log("I am a test string")).to.not.throw();
    });
    it("should log sequence of strings", function () {
        expect(() => console.log("I", "am", "a", "test", "string", 2, 2.345, { foo: "bar" })).to.not.throw();
    });
    it("Should log string substitutions", function () {
        expect(() => console.log("String sub: %s, float sub: %f, int sub: %d", "string", 3.1457, 3.1457)).to.not.throw();
    });
    it("Should allow numbers to substitute strings", function () {
        expect(() => console.log("Print these numbers! %s %s", 1.2345, 1)).to.not.throw();
    });
    it("Should allow strings to substitute numbers", function () {
        expect(() => console.log("Print these strings! %f %d", "foo", "bar")).to.not.throw();
    });
    it("Should allow for less substitution arguments than parameters", function () {
        expect(() => console.log("%s", "I am a string", 12345)).to.not.throw();
    });
    it("Should allow for more substitution arguments than parameters", function () {
        expect(() => console.log("%s %s", "I am a string")).to.not.throw();
    });
    it("Should allow a substitution argument with no parameter", function () {
        expect(() => console.log("%s")).to.not.throw();
    });
    it("Should ignore invalid substitution arguments", function () {
        expect(() => console.log("%y %s %k", "I am a string")).to.not.throw();
        expect(() => console.log("%y%s%k", "I am a string")).to.not.throw();
        expect(() => console.log("%", 756)).to.not.throw();
        expect(() => console.log("%%", 756)).to.not.throw();
    });
    it("Should allow logging NaN", function () {
        expect(() => console.log("%d %f %s", NaN, NaN, NaN)).to.not.throw();
        expect(() => console.log("%d %f %s", 0 / 0, 0 / 0, 0 / 0)).to.not.throw();
    });
});

describe("Web Streams", function () {
    // Focused ports from the WHATWG Streams WPT suites at c05b4473:
    // readable-streams/general.any.js and tee.any.js,
    // writable-streams/write.any.js, and transform-streams/general.any.js.
    it("installs the standard stream constructors", function () {
        expect(ReadableStream).to.be.a("function");
        expect(WritableStream).to.be.a("function");
        expect(TransformStream).to.be.a("function");
        expect(ByteLengthQueuingStrategy).to.be.a("function");
        expect(CountQueuingStrategy).to.be.a("function");
    });

    it("delivers queued chunks in order and closes the reader", async function () {
        const stream = new ReadableStream({
            start(controller) {
                controller.enqueue("a");
                controller.enqueue("b");
                controller.close();
            }
        });
        const reader = stream.getReader();

        expect(await reader.read()).to.deep.equal({ value: "a", done: false });
        expect(await reader.read()).to.deep.equal({ value: "b", done: false });
        expect(await reader.read()).to.deep.equal({ value: undefined, done: true });
        await reader.closed;
    });

    it("propagates a rejected pull to read and closed", async function () {
        const failure = new Error("pull failed");
        const reader = new ReadableStream({
            pull() {
                return Promise.reject(failure);
            }
        }).getReader();

        let readFailure: unknown;
        let closedFailure: unknown;
        try { await reader.read(); } catch (error) { readFailure = error; }
        try { await reader.closed; } catch (error) { closedFailure = error; }
        expect(readFailure).to.equal(failure);
        expect(closedFailure).to.equal(failure);
    });

    it("tees without one branch consuming the other", async function () {
        const [first, second] = new ReadableStream({
            start(controller) {
                controller.enqueue("a");
                controller.enqueue("b");
                controller.close();
            }
        }).tee();
        const firstReader = first.getReader();
        const secondReader = second.getReader();

        expect(await firstReader.read()).to.deep.equal({ value: "a", done: false });
        expect(await firstReader.read()).to.deep.equal({ value: "b", done: false });
        expect(await firstReader.read()).to.deep.equal({ value: undefined, done: true });
        expect(await secondReader.read()).to.deep.equal({ value: "a", done: false });
    });

    it("waits for asynchronous writes before closing", async function () {
        const stored: number[] = [];
        const writable = new WritableStream({
            write(chunk) {
                return Promise.resolve().then(() => stored.push(chunk));
            }
        });
        const writer = writable.getWriter();

        writer.write(1);
        writer.write(2);
        await writer.close();
        expect(stored).to.deep.equal([1, 2]);
    });

    it("applies transform output and backpressure", async function () {
        const transform = new TransformStream({
            transform(chunk, controller) {
                controller.enqueue(chunk.toUpperCase());
            }
        });
        const writer = transform.writable.getWriter();
        const reader = transform.readable.getReader();
        const write = writer.write("native");

        expect(await reader.read()).to.deep.equal({ value: "NATIVE", done: false });
        await write;
        await writer.close();
        expect(await reader.read()).to.deep.equal({ value: undefined, done: true });
    });

    it("supports BYOB reads from byte streams", async function () {
        let sent = false;
        const stream = new ReadableStream({
            type: "bytes",
            pull(controller) {
                if (!sent) {
                    sent = true;
                    controller.enqueue(new Uint8Array([8, 241, 48, 123, 151]));
                    controller.close();
                }
            }
        } as any);
        const reader = stream.getReader({ mode: "byob" });
        const result = await reader.read(new Uint8Array(8));

        expect(result.done).to.equal(false);
        expect(Array.from(result.value!)).to.deep.equal([8, 241, 48, 123, 151]);
        expect((await reader.read(new Uint8Array(8))).done).to.equal(true);
    });

    // Ported from Firefox's dom/streams/test/xpcshell/subclassing.js.
    it("supports subclassed streams, readers, and queuing strategies", async function () {
        class SubclassedStream extends ReadableStream {}
        class SubclassedStrategy extends CountQueuingStrategy {}
        const stream = new SubclassedStream({
            start(controller) {
                controller.enqueue("first");
                controller.close();
            }
        });
        const Reader = stream.getReader().constructor as typeof ReadableStreamDefaultReader;
        class SubclassedReader extends Reader {}

        expect(stream).to.be.instanceOf(ReadableStream);
        expect(new SubclassedStrategy({ highWaterMark: 4 }).highWaterMark).to.equal(4);

        const secondStream = new ReadableStream({
            start(controller) {
                controller.enqueue("second");
                controller.close();
            }
        });
        const reader = new SubclassedReader(secondStream);
        expect(await reader.read()).to.deep.equal({ value: "second", done: false });
    });

    // Ported from Chromium's http/tests/streams/chromium/transform-stream-enqueue.html.
    it("rejects enqueues after a transform is terminated or errored", function () {
        expect(() => new TransformStream({
            start(controller) {
                controller.terminate();
                controller.enqueue("late");
            }
        })).to.throw(TypeError);

        expect(() => new TransformStream({
            start(controller) {
                controller.error(new Error("failed"));
                controller.enqueue("late");
            }
        })).to.throw(TypeError);
    });
});

describe("Blob", function () {
    this.timeout(10000);

    let emptyBlobs: Blob[], helloBlobs: Blob[], stringBlob: Blob, typedArrayBlob: Blob, arrayBufferBlob: Blob, blobBlob: Blob;

    async function readStream(stream: ReadableStream<Uint8Array>, mode?: "byob"): Promise<number[]> {
        const reader: any = stream.getReader(mode === undefined ? undefined : { mode });
        const bytes: number[] = [];
        while (true) {
            const result = mode === "byob"
                ? await reader.read(new Uint8Array(64))
                : await reader.read();
            if (result.done) {
                return bytes;
            }
            bytes.push(...Array.from(result.value as Uint8Array));
        }
    }

    before(function () {
        emptyBlobs = [new Blob([]), new Blob([])];
        stringBlob = new Blob(["Hello"]);
        typedArrayBlob = new Blob([new Uint8Array([72, 101, 108, 108, 111])]),
        arrayBufferBlob = new Blob([new Uint8Array([72, 101, 108, 108, 111]).buffer]),
        blobBlob = new Blob([new Blob(["Hello"])]),
        helloBlobs = [stringBlob, typedArrayBlob, arrayBufferBlob, blobBlob]
    });

    // -------------------------------- Blob Construction --------------------------------
    it("creates empty blobs", function () {
        for (const blob of emptyBlobs) {
            expect(blob.size).to.equal(0);
            expect(blob.type).to.equal("");
        }
    });

    it("creates blob from string array", function () {
        expect(stringBlob.size).to.equal(5);
        expect(stringBlob.type).to.equal("");
    });

    it("creates blob from TypedArray", function () {
        expect(typedArrayBlob.size).to.equal(5);
        expect(typedArrayBlob.type).to.equal("");
    });

    it("creates blob from ArrayBuffer", function () {
        expect(arrayBufferBlob.size).to.equal(5);
        expect(arrayBufferBlob.type).to.equal("");
    });

    it("creates blob from another Blob", function () {
        expect(blobBlob.size).to.equal(5);
        expect(blobBlob.type).to.equal("");
    });

    it("applies MIME type from options", function () {
        const modelGltfJson = new Blob(["glTF"], { type: "model/gltf+json" })
        expect(modelGltfJson.type).to.equal("model/gltf+json");
    });

    // Focused ports from WPT FileAPI/blob/Blob-constructor.any.js.
    it("accepts iterable parts and preserves their order", async function () {
        const parts = {
            *[Symbol.iterator]() {
                yield "foo";
                yield new Uint8Array([98, 97, 114]);
                yield new Blob(["baz"]);
            }
        };
        const blob = new Blob(parts as any);
        expect(blob.size).to.equal(9);
        expect(await blob.text()).to.equal("foobarbaz");
    });

    it("uses an Array's overridden iterator", async function () {
        const parts = ["ignored"];
        parts[Symbol.iterator] = function* () {
            yield "custom";
        };

        expect(await new Blob(parts).text()).to.equal("custom");
    });

    it("closes an iterator when BlobPart conversion throws", function () {
        let closed = false;
        const badPart = {
            toString() {
                throw new Error("part conversion failed");
            }
        };
        const parts = (function* () {
            try {
                yield badPart;
            } finally {
                closed = true;
            }
        })();

        // QuickJS currently wraps an exception rethrown through a native
        // constructor as its generic JS error type, so assert the observable
        // iterator-close behavior separately from the adapter's error text.
        expect(() => new Blob(parts as any)).to.throw();
        expect(closed).to.equal(true);
    });

    it("observes BlobPart array mutations during iteration", async function () {
        const parts: any[] = [
            {
                toString() {
                    parts.pop();
                    return "PASS";
                }
            },
            {
                toString() {
                    throw new Error("removed part was converted");
                }
            }
        ];

        expect(await new Blob(parts).text()).to.equal("PASS");
    });

    it("converts parts before reading options in WebIDL order", function () {
        const accesses: string[] = [];
        const part = {
            toString() {
                accesses.push("part");
                return "data";
            }
        };
        new Blob([part], {
            get type() {
                accesses.push("type");
                return "TEXT/PLAIN";
            },
            get endings() {
                accesses.push("endings");
                return "transparent" as EndingType;
            }
        });

        expect(accesses).to.deep.equal(["part", "endings", "type"]);
    });

    it("validates the endings enum and options dictionary", function () {
        expect(() => new Blob([], { endings: "NATIVE" as EndingType })).to.throw();
        expect(() => new Blob([], { endings: "invalid" as EndingType })).to.throw();
        for (const value of [123, true, "abc"]) {
            expect(() => new Blob([], value as any)).to.throw();
        }
        expect(() => new Blob([], null as any)).not.to.throw();
        expect(() => new Blob([], undefined)).not.to.throw();
    });

    it("exposes browser-compatible class tags", function () {
        expect(String(new Blob())).to.equal("[object Blob]");
        expect(String(new File([], "empty.txt"))).to.equal("[object File]");
    });

    it("rejects primitive parts containers", function () {
        for (const value of [null, true, 7, "not a sequence"]) {
            // Some Node-API adapters currently wrap a Napi::TypeError thrown
            // by a constructor as their generic JS error type.
            expect(() => new Blob(value as any)).to.throw();
        }
    });

    it("normalizes valid MIME types and clears invalid types", function () {
        expect(new Blob([], { type: "TEXT/PLAIN" }).type).to.equal("text/plain");
        expect(new Blob([], { type: "te\x09xt/plain" }).type).to.equal("");
        expect(new Blob([], { type: "text/\x7fplain" }).type).to.equal("");
    });

    // -------------------------------- Blob.text() --------------------------------
    it("returns empty string for empty blobs", async function () {
        for (const blob of emptyBlobs) {
            const text = await blob.text();
            expect(text).to.equal("");
        }
    });

    it("returns correct string content for non-empty blobs", async function () {
        for (const blob of helloBlobs) {
            const text = await blob.text();
            expect(text).to.equal("Hello");
        }
    });

    it("handles multi-byte UTF-8 characters", async function () {
        const utf8Blob = new Blob(["你好, 世界"]);
        const text = await utf8Blob.text();
        expect(text).to.equal("你好, 世界");
    });

    it("replaces invalid UTF-8 bytes", async function () {
        const invalid = new Uint8Array([192, 193, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255]);
        expect(await new Blob([invalid]).text()).to.equal("\ufffd".repeat(invalid.length));
    });

    it("preserves line endings like default transparent mode", async function () {
        const lineEndingsBlob = new Blob(["Hello\nWorld"]);
        const text = await lineEndingsBlob.text();
        expect(text).to.equal("Hello\nWorld");
    });

    // -------------------------------- Blob.bytes() --------------------------------
    it("returns empty Uint8Array for empty blobs", async function () {
        for (const blob of emptyBlobs) {
            const bytes = await blob.bytes();
            expect(bytes).to.be.instanceOf(Uint8Array);
            expect(bytes.length).to.equal(0);
        }
    });

    it("returns correct byte content from non-empty blobs", async function () {
        for (const blob of helloBlobs) {
            const bytes = await blob.bytes();
            expect(bytes).to.be.instanceOf(Uint8Array);
            expect(bytes.length).to.equal(5);
            expect(bytes[0]).to.equal(72); // 'H'
            expect(bytes[4]).to.equal(111); // 'o'
        }
    });

    // -------------------------------- Blob.arrayBuffer() --------------------------------
    it("returns empty buffer for empty blobs", async function () {
        for (const blob of emptyBlobs) {
            const buffer = await blob.arrayBuffer();
            expect(buffer).to.be.instanceOf(ArrayBuffer);
            expect(buffer.byteLength).to.equal(0);
        }
    });

    it("returns correct buffer content for non-empty blobs", async function () {
        for (const blob of helloBlobs) {
            const buffer = await blob.arrayBuffer();
            expect(buffer).to.be.instanceOf(ArrayBuffer);
            expect(buffer.byteLength).to.equal(5);

            const view = new Uint8Array(buffer);
            expect(view[0]).to.equal(72); // 'H'
            expect(view[4]).to.equal(111); // 'o'

        }
    });

    // Focused ports from WPT FileAPI/blob/Blob-slice.any.js.
    it("slices across part boundaries and applies clamp rounding", async function () {
        const blob = new Blob(["foo", new Blob(["bar"]), "baz"]);
        expect(await blob.slice(2, 7).text()).to.equal("obarb");
        expect(await new Blob(["abcd"]).slice(1.5).text()).to.equal("cd");
        expect(await new Blob(["abcd"]).slice(2.5).text()).to.equal("cd");
        expect(await blob.slice(-3, undefined, "TEXT/PLAIN").text()).to.equal("baz");
        expect(blob.slice(-3, undefined, "TEXT/PLAIN").type).to.equal("text/plain");
    });

    // Focused ports from WPT FileAPI/blob/Blob-stream.any.js.
    it("streams binary data through default and BYOB readers", async function () {
        const input = [8, 241, 48, 123, 151];
        const blob = new Blob([new Uint8Array(input)]);
        expect(await readStream(blob.stream())).to.deep.equal(input);
        expect(await readStream(blob.stream(), "byob")).to.deep.equal(input);
        expect(await readStream(new Blob().stream())).to.deep.equal([]);
    });

    it("keeps independent stream cursors after the Blob reference is dropped", async function () {
        let blob: Blob | null = new Blob(["PASS"]);
        const first = blob.stream();
        const second = blob.stream();
        blob = null;
        expect(await readStream(first)).to.deep.equal([80, 65, 83, 83]);
        expect(await readStream(second)).to.deep.equal([80, 65, 83, 83]);
    });

    // Adapted from WebKit's fast/files/blob-stream-chunks.html.
    it("chunks a large Blob and supports cancellation without retaining work", async function () {
        const blob = new Blob([new Uint8Array(5 * 1024 * 1024)]);
        const reader = blob.stream().getReader();
        const first = await reader.read();
        expect(first.done).to.equal(false);
        expect(first.value!.byteLength).to.be.at.most(64 * 1024);
        await reader.cancel();
        await reader.closed;
    });

    // Adapted from WebKit's blob-stream crash regression and exercises
    // teardown of the C++ pull-state closures under repeated construction.
    it("constructs and cancels many empty streams without crashing", async function () {
        for (let index = 0; index < 1000; ++index) {
            await new Blob().stream().cancel();
        }
    });

    // Scaled port of Firefox's dom/streams/test/xpcshell/large-pipeto.js.
    it("pipes nested shared Blob parts without corrupting chunk boundaries", async function () {
        const pattern = new Uint8Array(256 * 1024);
        for (let index = 0; index < pattern.length; ++index) {
            pattern[index] = index % 256;
        }
        const pair = new Blob([pattern, pattern]);
        const nested = new Blob([pair, pair, pair, pair, pair, pair]);
        let position = 0;

        await nested.stream().pipeTo(new WritableStream({
            write(chunk: Uint8Array) {
                for (const value of chunk) {
                    const expected = position % pattern.length % 256;
                    if (value !== expected) {
                        throw new Error(`Blob stream byte ${position}: expected ${expected}, received ${value}`);
                    }
                    ++position;
                }
            }
        }));
        expect(position).to.equal(pattern.length * 12);
    });

    it("uses a File's Blob bytes when composing parts", async function () {
        const file = new File(["a", "b"], "letters.txt");
        expect(await new Blob(["<", file, ">"]).text()).to.equal("<ab>");
    });
});

describe("napi class prototype isolation (#172)", function () {
    // Regression coverage for #172: napi-defined class constructors must each
    // get a fresh per-class object as their `.prototype` property. Previously
    // on JSC every napi class shared the global Object.prototype, so writes
    // to one class's prototype polluted every object and every plain `{}`
    // erroneously satisfied `instanceof` for every napi class.

    it("Blob.prototype is not the global Object.prototype", function () {
        expect(Blob.prototype).to.not.equal(Object.prototype);
    });

    it("Blob.prototype chains to Object.prototype", function () {
        expect(Object.getPrototypeOf(Blob.prototype)).to.equal(Object.prototype);
    });

    it("Blob.prototype.constructor points back to Blob", function () {
        expect(Blob.prototype.constructor).to.equal(Blob);
    });

    it("instances inherit from Blob.prototype", function () {
        const blob = new Blob([]);
        expect(Object.getPrototypeOf(blob)).to.equal(Blob.prototype);
    });

    it("plain objects are not instanceof Blob", function () {
        expect({} instanceof Blob).to.equal(false);
    });

    it("writes to Blob.prototype do not pollute Object.prototype", function () {
        const KEY = "__jrh172_blob_prototype_marker__";
        const proto = Blob.prototype as any;
        try {
            proto[KEY] = 1;
            expect(KEY in {}).to.equal(false);
        } finally {
            delete proto[KEY];
        }
    });
});


describe("Performance", function () {
    this.timeout(1000);

    it("should have performance global defined", function () {
        expect(performance).to.not.be.undefined;
    });

    it("should return a number from performance.now()", function () {
        const now = performance.now();
        expect(now).to.be.a("number");
    });

    it("should return a non-negative value from performance.now()", function () {
        const now = performance.now();
        expect(now).to.be.at.least(0);
    });

    it("should return increasing values from performance.now()", function (done) {
        const first = performance.now();
        setTimeout(() => {
            const second = performance.now();
            expect(second).to.be.greaterThan(first);
            done();
        }, 10);
    });

    it("should measure elapsed time accurately", function (done) {
        const start = performance.now();
        const delay = 50;
        setTimeout(() => {
            const elapsed = performance.now() - start;
            // setTimeout guarantees a minimum delay, not a maximum.
            // Only check the lower bound to avoid flakes on busy CI agents.
            expect(elapsed).to.be.at.least(delay - 5);
            done();
        }, delay);
    });

    it("should return sub-millisecond precision", function () {
        // Call performance.now() multiple times rapidly and check if we get fractional values
        const samples: number[] = [];
        for (let i = 0; i < 100; i++) {
            samples.push(performance.now());
        }
        // At least some samples should have fractional parts (sub-millisecond precision)
        const hasFractional = samples.some(s => s % 1 !== 0);
        expect(hasFractional).to.equal(true);
    });
});

describe("TextDecoder", function () {
    it("should decode a Uint8Array to a string", function () {
        const decoder = new TextDecoder();
        const encoded = new Uint8Array([72, 101, 108, 108, 111]); // "Hello"
        const result = decoder.decode(encoded);
        expect(result).to.equal("Hello");
    });

    it("should decode an empty Uint8Array to an empty string", function () {
        const decoder = new TextDecoder();
        const result = decoder.decode(new Uint8Array([]));
        expect(result).to.equal("");
    });

    it("should decode an ArrayBuffer to a string", function () {
        const decoder = new TextDecoder();
        const buffer = new Uint8Array([87, 111, 114, 108, 100]).buffer; // "World"
        const result = decoder.decode(buffer);
        expect(result).to.equal("World");
    });

    it("should decode a TypedArray subarray with non-zero byteOffset", function () {
        const decoder = new TextDecoder();
        const full = new Uint8Array([88, 72, 105]); // "XHi"
        const sub = full.subarray(1); // [72, 105] -> "Hi"
        const result = decoder.decode(sub);
        expect(result).to.equal("Hi");
    });

    it("should decode a Uint8Array containing a null byte", function () {
        const decoder = new TextDecoder();
        const encoded = new Uint8Array([72, 0, 105]); // "H\0i"
        const result = decoder.decode(encoded);
        expect(result).to.equal("H\0i");
        expect(result.length).to.equal(3);
    });

    it("throwing from the constructor repeatedly does not corrupt native state", function () {
        // Regression for a Chakra N-API ObjectWrap bug: when a wrapped
        // constructor throws, the native instance is destroyed during stack
        // unwinding but the wrap finalizer stayed attached to `this`, so a
        // later GC ran the finalizer on freed memory (heap corruption). Throw
        // many times to create many dangling wraps, then allocate/decode to
        // exercise the heap and surface any corruption within this test run.
        for (let i = 0; i < 100; ++i) {
            expect(() => new TextDecoder("utf-16")).to.throw();
        }
        const decoder = new TextDecoder("utf-8");
        expect(decoder.decode(new Uint8Array([79, 75]))).to.equal("OK");
    });

    it("should accept the WHATWG 'utf8' label (no hyphen)", function () {
        const decoder = new TextDecoder("utf8");
        const result = decoder.decode(new Uint8Array([72, 105])); // "Hi"
        expect(result).to.equal("Hi");
    });

    it("should accept utf-8 labels case-insensitively and with surrounding whitespace", function () {
        for (const label of ["UTF-8", "UTF8", "  utf-8  ", "\tUtf8\n"]) {
            const decoder = new TextDecoder(label);
            expect(decoder.decode(new Uint8Array([79, 75]))).to.equal("OK");
        }
    });

    it("should accept the other WHATWG utf-8 aliases", function () {
        for (const label of ["unicode-1-1-utf-8", "unicode11utf8", "unicode20utf8", "x-unicode20utf8"]) {
            const decoder = new TextDecoder(label);
            expect(decoder.decode(new Uint8Array([79, 75]))).to.equal("OK");
        }
    });

    it("should still throw for a genuinely unsupported encoding", function () {
        expect(() => new TextDecoder("utf-16")).to.throw();
    });
});

describe("TextEncoder", function () {
    it("should expose encoding === 'utf-8'", function () {
        const encoder = new TextEncoder();
        expect(encoder.encoding).to.equal("utf-8");
    });

    it("should encode an ASCII string into UTF-8 bytes", function () {
        const encoder = new TextEncoder();
        const bytes = encoder.encode("Hello");
        expect(Array.from(bytes)).to.eql([72, 101, 108, 108, 111]);
    });

    it("should return an empty Uint8Array when called with no argument", function () {
        const encoder = new TextEncoder();
        const bytes = encoder.encode();
        expect(bytes.length).to.equal(0);
    });

    it("should return an empty Uint8Array for undefined input", function () {
        const encoder = new TextEncoder();
        const bytes = encoder.encode(undefined);
        expect(bytes.length).to.equal(0);
    });

    it("should encode a multi-byte UTF-8 string", function () {
        const encoder = new TextEncoder();
        // "é" is U+00E9 -> 0xC3 0xA9 in UTF-8
        const bytes = encoder.encode("é");
        expect(Array.from(bytes)).to.eql([0xC3, 0xA9]);
    });

    it("should encode a string containing a null byte", function () {
        const encoder = new TextEncoder();
        const bytes = encoder.encode("H\0i");
        expect(Array.from(bytes)).to.eql([72, 0, 105]);
    });
});

declare const File: any;
declare const FileReader: any;

describe("File", function () {
    // -------------------------------- Construction --------------------------------
    it("creates an empty File", function () {
        const file = new File([], "empty.txt");
        expect(file.size).to.equal(0);
        expect(file.type).to.equal("");
        expect(file.name).to.equal("empty.txt");
    });

    it("creates a File from a string array", function () {
        const file = new File(["Hello"], "hello.txt");
        expect(file.size).to.equal(5);
        expect(file.name).to.equal("hello.txt");
    });

    it("creates a File from a TypedArray", function () {
        const data = new Uint8Array([72, 101, 108, 108, 111]); // "Hello"
        const file = new File([data], "typed.bin");
        expect(file.size).to.equal(5);
        expect(file.name).to.equal("typed.bin");
    });

    it("creates a File from an ArrayBuffer", function () {
        const buffer = new Uint8Array([72, 101, 108, 108, 111]).buffer;
        const file = new File([buffer], "buffer.bin");
        expect(file.size).to.equal(5);
    });

    it("creates a File from a Blob", function () {
        const blob = new Blob(["Hello"]);
        const file = new File([blob], "from-blob.txt");
        expect(file.size).to.equal(5);
    });

    it("applies MIME type from options", function () {
        const file = new File(["{}"], "data.json", { type: "application/json" });
        expect(file.type).to.equal("application/json");
    });

    it("defaults lastModified to a recent timestamp when not provided", function () {
        const before = Date.now();
        const file = new File([], "x.txt");
        const after = Date.now();
        expect(file.lastModified).to.be.a("number");
        // Allow small clock-skew slack on either side.
        expect(file.lastModified).to.be.at.least(before - 1000);
        expect(file.lastModified).to.be.at.most(after + 1000);
    });

    it("honors lastModified from options", function () {
        const file = new File([], "x.txt", { lastModified: 12345 });
        expect(file.lastModified).to.equal(12345);
    });

    it("coerces a non-string name to a string", function () {
        const file = new File([], 42 as any);
        expect(file.name).to.equal("42");
    });

    it("coerces undefined and null name per WebIDL USVString", function () {
        // Per the WHATWG File constructor's WebIDL signature, name is a
        // non-optional USVString; ToString is applied regardless of input
        // type, so passing undefined/null yields the string "undefined" /
        // "null" rather than an empty string.
        expect(new File([], undefined as any).name).to.equal("undefined");
        expect(new File([], null as any).name).to.equal("null");
    });

    // TODO(JsRH#175): Re-enable once the Chakra Node-API shim surfaces
    // exceptions thrown from class constructor callbacks back to JS.
    // it("throws when fewer than 2 arguments are passed", function () {
    //     // File requires both fileBits and fileName per the WebIDL bindings.
    //     // Browsers throw TypeError on missing arguments; the native polyfill
    //     // must match that surface so consumers don't accidentally create a
    //     // File with empty name when their call site is misspelled.
    //     // Note: we only assert *that* it throws (not the specific error
    //     // type), because the JSI napi shim wraps thrown Napi::TypeError as
    //     // a generic JS Error when surfacing it across the host boundary.
    //     expect(() => new (File as any)()).to.throw();
    //     expect(() => new (File as any)([])).to.throw();
    // });

    // -------------------------------- Read API --------------------------------
    it("returns text via .text()", async function () {
        const file = new File(["Hello"], "hello.txt");
        const text = await file.text();
        expect(text).to.equal("Hello");
    });

    it("returns bytes via .bytes()", async function () {
        const file = new File(["Hello"], "hello.txt");
        const bytes = await file.bytes();
        expect(bytes).to.be.instanceOf(Uint8Array);
        expect(bytes.length).to.equal(5);
        expect(bytes[0]).to.equal(72); // 'H'
        expect(bytes[4]).to.equal(111); // 'o'
    });

    it("returns an ArrayBuffer via .arrayBuffer()", async function () {
        const file = new File(["Hello"], "hello.txt");
        const buffer = await file.arrayBuffer();
        expect(buffer).to.be.instanceOf(ArrayBuffer);
        expect(buffer.byteLength).to.equal(5);
    });

    it("handles multi-byte UTF-8 content", async function () {
        const file = new File(["你好, 世界"], "utf8.txt");
        const text = await file.text();
        expect(text).to.equal("你好, 世界");
    });

    // Adapted from WebKit's fast/files/blob-stream-crash-2.html.
    it("streams and slices multiple File parts through the Blob API", async function () {
        const file = new File(["a", new Blob(), "b", new Blob(), "c", new Blob(), "d"], "letters.txt");
        const reader = file.stream().getReader();
        const bytes: number[] = [];
        while (true) {
            const result = await reader.read();
            if (result.done) {
                break;
            }
            bytes.push(...Array.from(result.value as Uint8Array));
        }
        expect(bytes).to.deep.equal([97, 98, 99, 100]);
        expect(await file.slice(1, 3).text()).to.equal("bc");
    });

    // -------------------------------- Blob inheritance --------------------------------
    it("is an instance of Blob (prototype chain wired up)", function () {
        // BJS core (fileTools, Offline/database, abstractEngine,
        // thinNativeEngine) branches on `instanceof Blob`. File must
        // satisfy that check for File inputs to take the Blob path,
        // matching the WHATWG spec where File is a Blob subtype.
        const file = new File(["x"], "x.txt");
        expect(file instanceof Blob).to.equal(true);
        expect(file instanceof File).to.equal(true);
    });
});

describe("FileReader", function () {
    // -------------------------------- State constants --------------------------------
    it("exposes EMPTY / LOADING / DONE as static constants", function () {
        expect(FileReader.EMPTY).to.equal(0);
        expect(FileReader.LOADING).to.equal(1);
        expect(FileReader.DONE).to.equal(2);
    });

    it("exposes EMPTY / LOADING / DONE on instances", function () {
        const reader = new FileReader();
        expect(reader.EMPTY).to.equal(0);
        expect(reader.LOADING).to.equal(1);
        expect(reader.DONE).to.equal(2);
    });

    it("does not pollute Object.prototype with EMPTY/LOADING/DONE", function () {
        // Regression: in earlier drafts the JSC napi shim's
        // func.Get("prototype") returns Object.prototype, so writing
        // EMPTY/LOADING/DONE through it pollutes every plain object's
        // for..in iteration and breaks consumers like Babylon.js's
        // CameraInputsManager.attachElement.
        const plain: any = {};
        const keys: string[] = [];
        for (const k in plain) keys.push(k);
        expect(keys).to.have.lengthOf(0);

        // And the keys must not be present as inherited enumerable
        // properties on a fresh object either.
        expect("EMPTY" in plain && !Object.prototype.hasOwnProperty.call(plain, "EMPTY"))
            .to.equal(false);
    });

    // -------------------------------- Initial state --------------------------------
    it("initializes with EMPTY readyState and null result/error", function () {
        const reader = new FileReader();
        expect(reader.readyState).to.equal(FileReader.EMPTY);
        expect(reader.result).to.equal(null);
        expect(reader.error).to.equal(null);
    });

    it("provides null on* event handler slots by default", function () {
        const reader = new FileReader();
        expect(reader.onloadstart).to.equal(null);
        expect(reader.onprogress).to.equal(null);
        expect(reader.onload).to.equal(null);
        expect(reader.onabort).to.equal(null);
        expect(reader.onerror).to.equal(null);
        expect(reader.onloadend).to.equal(null);
    });

    // -------------------------------- readAsText --------------------------------
    it("reads a Blob as text via onload", function (done) {
        const reader = new FileReader();
        const blob = new Blob(["Hello"]);
        reader.onload = function () {
            try {
                expect(reader.readyState).to.equal(FileReader.DONE);
                expect(reader.result).to.equal("Hello");
                done();
            } catch (e) {
                done(e);
            }
        };
        reader.readAsText(blob);
    });

    it("reads a File as text via onload", function (done) {
        const reader = new FileReader();
        const file = new File(["World"], "world.txt");
        reader.onload = function () {
            try {
                expect(reader.result).to.equal("World");
                done();
            } catch (e) {
                done(e);
            }
        };
        reader.readAsText(file);
    });

    it("fires onloadend after onload", function (done) {
        const reader = new FileReader();
        const blob = new Blob(["abc"]);
        let loadFired = false;
        reader.onload = function () {
            loadFired = true;
        };
        reader.onloadend = function () {
            try {
                expect(loadFired).to.equal(true);
                expect(reader.readyState).to.equal(FileReader.DONE);
                done();
            } catch (e) {
                done(e);
            }
        };
        reader.readAsText(blob);
    });

    // -------------------------------- readAsArrayBuffer --------------------------------
    it("reads a Blob as an ArrayBuffer via onload", function (done) {
        const reader = new FileReader();
        const blob = new Blob([new Uint8Array([1, 2, 3])]);
        reader.onload = function () {
            try {
                expect(reader.result).to.be.instanceOf(ArrayBuffer);
                expect(reader.result.byteLength).to.equal(3);
                const view = new Uint8Array(reader.result);
                expect(view[0]).to.equal(1);
                expect(view[2]).to.equal(3);
                done();
            } catch (e) {
                done(e);
            }
        };
        reader.readAsArrayBuffer(blob);
    });

    // -------------------------------- readAsDataURL --------------------------------
    it("reads a Blob as a base64 data URL", function (done) {
        const reader = new FileReader();
        // "Hello" -> base64 SGVsbG8=
        const blob = new Blob(["Hello"], { type: "text/plain" });
        reader.onload = function () {
            try {
                expect(reader.result).to.be.a("string");
                expect(reader.result).to.equal("data:text/plain;base64,SGVsbG8=");
                done();
            } catch (e) {
                done(e);
            }
        };
        reader.readAsDataURL(blob);
    });

    it("falls back to application/octet-stream when the source blob has no type", function (done) {
        const reader = new FileReader();
        const blob = new Blob(["Hello"]);
        reader.onload = function () {
            try {
                expect(reader.result).to.equal("data:application/octet-stream;base64,SGVsbG8=");
                done();
            } catch (e) {
                done(e);
            }
        };
        reader.readAsDataURL(blob);
    });

    // -------------------------------- addEventListener --------------------------------
    it("dispatches 'load' events to addEventListener listeners", function (done) {
        const reader = new FileReader();
        const blob = new Blob(["abc"]);
        let countA = 0;
        let countB = 0;
        const handlerA = function () {
            countA++;
        };
        const handlerB = function () {
            countB++;
        };
        reader.addEventListener("load", handlerA);
        reader.addEventListener("load", handlerB);
        // Per WHATWG, adding the same listener twice is a no-op, so handlerA
        // should still fire exactly once.
        reader.addEventListener("load", handlerA);
        reader.onloadend = function () {
            try {
                expect(countA).to.equal(1);
                expect(countB).to.equal(1);
                done();
            } catch (e) {
                done(e);
            }
        };
        reader.readAsText(blob);
    });

    it("does not call a listener after removeEventListener", function (done) {
        const reader = new FileReader();
        const blob = new Blob(["abc"]);
        let called = false;
        const handler = function () {
            called = true;
        };
        reader.addEventListener("load", handler);
        reader.removeEventListener("load", handler);
        reader.onloadend = function () {
            try {
                expect(called).to.equal(false);
                done();
            } catch (e) {
                done(e);
            }
        };
        reader.readAsText(blob);
    });

    // -------------------------------- abort --------------------------------
    it("transitions readyState to DONE after abort()", function (done) {
        const reader = new FileReader();
        const blob = new Blob(["abc"]);
        reader.readAsText(blob);
        // Immediately abort before the queued read completes.
        reader.abort();
        // Wait one microtask turn so any pending dispatch settles before we inspect state.
        Promise.resolve().then(() => {
            try {
                expect(reader.readyState).to.equal(FileReader.DONE);
                done();
            } catch (e) {
                done(e);
            }
        });
    });
});

function runTests() {
    mocha.run((failures: number) => {
        // Test program will wait for code to be set before exiting
        if (failures > 0) {
            // Failure
            setExitCode(1);
        } else {
            // Success
            setExitCode(0);
        }
    });
}

runTests();
