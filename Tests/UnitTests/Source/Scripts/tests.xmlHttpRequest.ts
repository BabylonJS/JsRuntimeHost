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
        const xhr: any = await createRequest("GET", "app:///Assets/symlink_target.js");
        expect(xhr.errorCode).to.equal("");
        expect(xhr.errorDetail).to.equal("");
    });

    for (const url of ["//", "noscheme.glb"]) {
        it(`should report a URL-open failure asynchronously for ${url}`, async function () {
            this.timeout(5000);
            const xhr = new XMLHttpRequest() as XMLHttpRequest & { errorCode: string; errorDetail: string };
            const events: string[] = [];
            xhr.addEventListener("readystatechange", () => events.push(`state:${xhr.readyState}`));
            xhr.open("GET", url);
            expect(xhr.readyState).to.equal(XMLHttpRequest.OPENED);
            expect(events).to.deep.equal(["state:1"]);
            xhr.send();
            expect(() => xhr.send()).to.throw();
            expect(events).to.deep.equal(["state:1"]);
            await new Promise<void>((resolve) => {
                xhr.addEventListener("error", () => events.push("error"));
                xhr.addEventListener("loadend", () => {
                    events.push("loadend");
                    resolve();
                });
            });
            expect(events).to.deep.equal(["state:1", "state:4", "error", "loadend"]);
            expect(xhr.status).to.equal(0);
            expect(xhr.statusText).to.equal("");
            expect(xhr.responseText).to.equal("");
            expect(xhr.errorCode).to.equal("UrlOpenFailed");
            expect(xhr.errorDetail).to.contain("Error opening URL:");
        });
    }

    it("should still reject an unsupported method synchronously", function () {
        const xhr = new XMLHttpRequest();
        expect(() => xhr.open("INVALID", "noscheme.glb")).to.throw();
        expect(xhr.readyState).to.equal(XMLHttpRequest.UNSENT);
    });

    it("should still reject an unsupported body after a URL-open failure", async function () {
        this.timeout(5000);
        const xhr = new XMLHttpRequest();
        xhr.open("GET", "noscheme.glb");
        expect(() => xhr.send(new Uint8Array(1))).to.throw();
        const completed = new Promise<void>((resolve) => xhr.addEventListener("loadend", () => resolve()));
        xhr.send();
        await completed;
        expect(xhr.status).to.equal(0);
    });

    it("should discard a pending URL-open failure when reopened", async function () {
        this.timeout(5000);
        const xhr = new XMLHttpRequest() as XMLHttpRequest & { errorCode: string; errorDetail: string };
        let errors = 0;
        xhr.addEventListener("error", () => errors++);
        xhr.open("GET", "noscheme.glb");
        xhr.send();
        xhr.open("GET", "app:///Assets/symlink_target.js");
        expect(xhr.errorCode).to.equal("");
        expect(xhr.errorDetail).to.equal("");
        const completed = new Promise<void>((resolve) => xhr.addEventListener("loadend", () => resolve()));
        xhr.send();
        await completed;
        expect(errors).to.equal(0);
        expect(xhr.status).to.equal(200);
        expect(xhr.responseText).to.equal("var symlink_target_js = true;");
    });

    it("should cancel a pending URL-open failure when aborted", async function () {
        this.timeout(5000);
        const xhr = new XMLHttpRequest();
        const events: string[] = [];
        xhr.addEventListener("error", () => events.push("error"));
        xhr.addEventListener("loadend", () => events.push("loadend"));
        xhr.open("GET", "noscheme.glb");
        xhr.send();
        xhr.abort();
        await new Promise<void>((resolve) => setTimeout(resolve, 10));
        expect(events).to.deep.equal([]);
    });

    for (const event of ["before send", "before dispatch", "readystatechange", "error"]) {
        it(`should release canceled URL-open listeners after aborting ${event}`, async function () {
            this.timeout(5000);
            const xhr = new XMLHttpRequest();
            let callbacks = 0;
            const listener = () => {
                callbacks++;
                if ((event === "readystatechange" || event === "error") && xhr.readyState === XMLHttpRequest.DONE) {
                    xhr.abort();
                }
            };
            xhr.addEventListener("readystatechange", listener);
            xhr.addEventListener("error", listener);
            if (event === "error") {
                xhr.removeEventListener("readystatechange", listener);
            }
            xhr.open("GET", "noscheme.glb");
            if (event === "before send") {
                xhr.abort();
            }
            xhr.send();
            if (event === "before dispatch") {
                xhr.abort();
            }
            await new Promise<void>((resolve) => setTimeout(resolve, 10));
            const beforeReopen = callbacks;
            xhr.open("GET", "app:///Assets/symlink_target.js");
            const completed = new Promise<void>((resolve) => xhr.addEventListener("loadend", () => resolve()));
            xhr.send();
            await completed;
            expect(xhr.readyState).to.equal(XMLHttpRequest.DONE);
            expect(callbacks).to.equal(beforeReopen);
        });
    }

    for (const event of ["readystatechange", "error"]) {
        it(`should preserve a request reopened from the failure ${event} callback`, async function () {
            this.timeout(5000);
            const xhr = new XMLHttpRequest();
            let reopened = false;
            let errors = 0;
            xhr.addEventListener("error", () => errors++);
            xhr.addEventListener(event, () => {
                if (!reopened && xhr.readyState === XMLHttpRequest.DONE) {
                    reopened = true;
                    xhr.open("GET", "app:///Assets/symlink_target.js");
                    xhr.send();
                }
            });
            const completed = new Promise<void>((resolve) => xhr.addEventListener("loadend", () => resolve()));
            xhr.open("GET", "noscheme.glb");
            xhr.send();
            await completed;
            expect(reopened).to.equal(true);
            expect(errors).to.equal(event === "error" ? 1 : 0);
            expect(xhr.status).to.equal(200);
            expect(xhr.responseText).to.equal("var symlink_target_js = true;");
        });
    }

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
});
