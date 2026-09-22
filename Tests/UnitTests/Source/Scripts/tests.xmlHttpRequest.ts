import { expect } from "chai";

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

    it("should fire 'load' rather than 'error' for a remote URL that returns HTTP 404", async function () {
        // Regression test: previously the success-only continuation in XMLHttpRequest::Send
        // skipped the completion events entirely on async failures, so observers never ran.
        // See https://github.com/BabylonJS/JsRuntimeHost/pull/165.
        //
        // A 404 is a *completed* HTTP transaction, so per spec it dispatches 'load' and callers
        // branch on xhr.status inside the handler; 'error' is reserved for transport-level
        // failures, which report status 0.
        this.timeout(30000);
        const result = await new Promise<{ errorFired: boolean; loadFired: boolean; loadendFired: boolean; status: number; readyState: number }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            let errorFired = false;
            let loadFired = false;
            let loadendFired = false;
            const guard = setTimeout(() => reject(new Error("XHR neither loaded nor loadended within 25s")), 25000);
            xhr.addEventListener("error", () => { errorFired = true; });
            xhr.addEventListener("load", () => { loadFired = true; });
            xhr.addEventListener("loadend", () => {
                loadendFired = true;
                clearTimeout(guard);
                resolve({ errorFired, loadFired, loadendFired, status: xhr.status, readyState: xhr.readyState });
            });
            xhr.open("GET", "https://github.com/babylonJS/BabylonNative404");
            xhr.send();
        });
        expect(result.status).to.equal(404);
        expect(result.loadFired).to.equal(true);
        expect(result.errorFired).to.equal(false);
        expect(result.loadendFired).to.equal(true);
        expect(result.readyState).to.equal(4);
    });

    it("should invoke the 'onreadystatechange' handler property", async function () {
        // Regression test: the on<event> handler properties were not implemented, so
        // `xhr.onreadystatechange = fn` set an ordinary expando property that was never
        // invoked and callers waited forever for a callback that could never fire.
        this.timeout(30000);
        const result = await new Promise<{ states: number[]; status: number }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            const states: number[] = [];
            const guard = setTimeout(() => reject(new Error("onreadystatechange never reached readyState 4 within 25s")), 25000);
            xhr.onreadystatechange = () => {
                states.push(xhr.readyState);
                if (xhr.readyState === 4) {
                    clearTimeout(guard);
                    resolve({ states, status: xhr.status });
                }
            };
            xhr.open("GET", "app:///Assets/symlink_target.js");
            xhr.send();
        });
        expect(result.states).to.include(4);
        expect(result.status).to.equal(200);
    });

    it("should invoke the 'onload' and 'onloadend' handler properties on success", async function () {
        this.timeout(30000);
        const result = await new Promise<{ loadFired: boolean; loadEndFired: boolean; errorFired: boolean }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            let loadFired = false;
            let errorFired = false;
            const guard = setTimeout(() => reject(new Error("onloadend did not fire within 25s")), 25000);
            xhr.onload = () => { loadFired = true; };
            xhr.onerror = () => { errorFired = true; };
            xhr.onloadend = () => {
                clearTimeout(guard);
                resolve({ loadFired, loadEndFired: true, errorFired });
            };
            xhr.open("GET", "app:///Assets/symlink_target.js");
            xhr.send();
        });
        expect(result.loadFired).to.equal(true);
        expect(result.loadEndFired).to.equal(true);
        expect(result.errorFired).to.equal(false);
    });

    it("should invoke the 'onload' handler property, not 'onerror', for HTTP 404", async function () {
        // 'error' means the transfer never completed. A 404 completed and carries a status, so
        // the load handler runs and inspects xhr.status.
        this.timeout(30000);
        const result = await new Promise<{ errorFired: boolean; loadFired: boolean; status: number }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            let errorFired = false;
            let loadFired = false;
            const guard = setTimeout(() => reject(new Error("onloadend did not fire within 25s")), 25000);
            xhr.onerror = () => { errorFired = true; };
            xhr.onload = () => { loadFired = true; };
            xhr.onloadend = () => {
                clearTimeout(guard);
                resolve({ errorFired, loadFired, status: xhr.status });
            };
            xhr.open("GET", "https://github.com/babylonJS/BabylonNative404");
            xhr.send();
        });
        expect(result.status).to.equal(404);
        expect(result.loadFired).to.equal(true);
        expect(result.errorFired).to.equal(false);
    });

    it("should let an on<event> property be read back, replaced, and cleared", async function () {
        const xhr = new XMLHttpRequest();
        expect(xhr.onload).to.equal(null);

        const first = () => { };
        xhr.onload = first;
        expect(xhr.onload).to.equal(first);

        // Assignment replaces rather than accumulates, unlike addEventListener.
        const second = () => { };
        xhr.onload = second;
        expect(xhr.onload).to.equal(second);

        xhr.onload = null;
        expect(xhr.onload).to.equal(null);
    });

    it("should coerce a non-callable on<event> assignment to null", function () {
        // EventHandler attributes are [LegacyTreatNonObjectAsNull] in WebIDL: assigning a
        // non-callable value clears the handler rather than throwing a TypeError.
        const xhr: any = new XMLHttpRequest();
        xhr.onload = () => { };
        expect(xhr.onload).to.not.equal(null);

        xhr.onload = 0;
        expect(xhr.onload).to.equal(null);

        xhr.onload = () => { };
        xhr.onload = "not a function";
        expect(xhr.onload).to.equal(null);

        xhr.onload = () => { };
        xhr.onload = undefined;
        expect(xhr.onload).to.equal(null);
    });

    it("should reject a non-callable object assigned to an on<event> property", function () {
        const xhr: any = new XMLHttpRequest();
        const handler = () => { };
        xhr.onload = handler;

        expect(() => { xhr.onload = {}; }).to.throw(TypeError);
        expect(xhr.onload).to.equal(handler);
    });

    it("should fire 'abort' rather than 'error' when a request is aborted", async function () {
        this.timeout(30000);
        const result = await new Promise<{ abortFired: boolean; errorFired: boolean; loadFired: boolean; loadEndFired: boolean }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            let abortFired = false;
            let errorFired = false;
            let loadFired = false;
            const guard = setTimeout(() => reject(new Error("loadend did not fire within 25s")), 25000);
            xhr.onabort = () => { abortFired = true; };
            xhr.onerror = () => { errorFired = true; };
            xhr.onload = () => { loadFired = true; };
            xhr.onloadend = () => {
                clearTimeout(guard);
                resolve({ abortFired, errorFired, loadFired, loadEndFired: true });
            };
            xhr.open("GET", "https://github.com/");
            xhr.send();
            xhr.abort();
        });
        // loadend must always settle the request, whatever the outcome.
        expect(result.loadEndFired).to.equal(true);
        // The abort was requested before the transfer could complete, so it must be reported
        // as an abort -- never as a transport error, and never as a successful load.
        expect(result.abortFired).to.equal(true);
        expect(result.errorFired).to.equal(false);
        expect(result.loadFired).to.equal(false);
    });

    it("should dispatch on<event> properties and addEventListener handlers in registration order", async function () {
        // on<event> handlers and addEventListener listeners share one list per event type, so
        // dispatch follows registration order across both styles rather than running all the
        // on<event> handlers first.
        this.timeout(30000);
        const result = await new Promise<{ order: string[] }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            const order: string[] = [];
            const guard = setTimeout(() => reject(new Error("loadend did not fire within 25s")), 25000);
            xhr.addEventListener("load", () => { order.push("first"); });
            xhr.onload = () => { order.push("onload"); };
            xhr.addEventListener("load", () => { order.push("last"); });
            xhr.addEventListener("loadend", () => {
                clearTimeout(guard);
                resolve({ order });
            });
            xhr.open("GET", "app:///Assets/symlink_target.js");
            xhr.send();
        });
        expect(result.order).to.deep.equal(["first", "onload", "last"]);
    });

    it("should keep an on<event> handler's position in the dispatch order when reassigned", async function () {
        // Per HTML the internal listener is registered on first set and reused thereafter ("If
        // eventHandler's listener is not null, then return"), so reassigning the property must
        // not move it to the end of the list.
        this.timeout(30000);
        const result = await new Promise<{ order: string[] }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            const order: string[] = [];
            const guard = setTimeout(() => reject(new Error("loadend did not fire within 25s")), 25000);
            xhr.onload = () => { order.push("replaced"); };
            xhr.addEventListener("load", () => { order.push("listener"); });
            xhr.onload = () => { order.push("onload"); };
            xhr.addEventListener("loadend", () => {
                clearTimeout(guard);
                resolve({ order });
            });
            xhr.open("GET", "app:///Assets/symlink_target.js");
            xhr.send();
        });
        expect(result.order).to.deep.equal(["onload", "listener"]);
    });

    it("should invoke a function registered both as an on<event> property and via addEventListener twice", async function () {
        // These are two independent registrations, so the duplicate-registration check must not
        // see the on<event> entry: a browser calls the shared function once for each.
        this.timeout(30000);
        const result = await new Promise<{ calls: number }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            let calls = 0;
            const guard = setTimeout(() => reject(new Error("loadend did not fire within 25s")), 25000);
            const handler = () => { calls++; };
            xhr.onload = handler;
            xhr.addEventListener("load", handler);
            xhr.addEventListener("loadend", () => {
                clearTimeout(guard);
                resolve({ calls });
            });
            xhr.open("GET", "app:///Assets/symlink_target.js");
            xhr.send();
        });
        expect(result.calls).to.equal(2);
    });

    it("should treat a duplicate addEventListener registration as a no-op", async function () {
        // Per DOM, re-adding an identical (type, callback) pair is a silent no-op rather than an
        // error, and the listener stays registered once, so it is dispatched once.
        this.timeout(30000);
        const result = await new Promise<{ calls: number }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            let calls = 0;
            const guard = setTimeout(() => reject(new Error("loadend did not fire within 25s")), 25000);
            const handler = () => { calls++; };
            xhr.addEventListener("load", handler);
            expect(() => xhr.addEventListener("load", handler)).to.not.throw();
            xhr.addEventListener("loadend", () => {
                clearTimeout(guard);
                resolve({ calls });
            });
            xhr.open("GET", "app:///Assets/symlink_target.js");
            xhr.send();
        });
        expect(result.calls).to.equal(1);
    });

    it("should not let removeEventListener remove an on<event> handler", async function () {
        // The property is cleared by assigning null, not by removeEventListener.
        this.timeout(30000);
        const result = await new Promise<{ order: string[] }>((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            const order: string[] = [];
            const guard = setTimeout(() => reject(new Error("loadend did not fire within 25s")), 25000);
            const handler = () => { order.push("onload"); };
            xhr.onload = handler;
            xhr.removeEventListener("load", handler);
            xhr.addEventListener("loadend", () => {
                clearTimeout(guard);
                resolve({ order });
            });
            xhr.open("GET", "app:///Assets/symlink_target.js");
            xhr.send();
        });
        expect(result.order).to.deep.equal(["onload"]);
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
        const xhr: any = await createRequest("GET", "app:///Assets/symlink_target.js");
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
            const xhr = await createRequest("GET", "app:///Assets/symlink_1.js");
            expect(xhr).to.have.property("responseText", "var symlink_target_js = true;");
        });

        it("should load URL pointing to symlink that points to a symlink", async function () {
            const xhr = await createRequest("GET", "app:///Assets/symlink_2.js");
            expect(xhr).to.have.property("responseText", "var symlink_target_js = true;");
        });
    }

    it("should load URL as array buffer", async function () {
        const xhr = await createRequest("GET", "app:///Assets/symlink_target.js", undefined, "arraybuffer");
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

    it("should not truncate responseText or response at an embedded null byte", async function () {
        const xhr = await createRequest("GET", "app:///Assets/embedded_nulls.txt");
        expect(xhr.status).to.equal(200);
        expect(xhr.responseText).to.equal("start\0middle\0end");
        expect(xhr.responseText.length).to.equal(16);
        expect(xhr.response).to.equal(xhr.responseText);
    });
}
