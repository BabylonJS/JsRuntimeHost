import { expect } from "chai";
import { bytes, sendXhr } from "./httpTestHelpers";

describe("UrlLib HTTP transport", function () {
    this.timeout(15000);

    const body = JSON.stringify({ message: "caf\u00e9 \u20ac", nested: { ready: true } });

    describe("POST", function () {
        before(function () {
            if (hostPlatform === "Unix") {
                this.skip(); // UrlLib's Linux backend does not implement POST.
            }
        });

        const initializers: { name: string; contentType: string; headers: () => Promise<HeadersInit> }[] = [
            ...["Content-Type", "content-type", "CoNtEnT-TyPe"].map(name => ({
                name: `record ${name}`,
                contentType: "application/json",
                headers: async () => ({ [name]: "application/json", "X-Test": "post" }),
            })),
            {
                name: "header pairs with MIME parameters",
                contentType: "application/json; charset=utf-8",
                headers: async () => [["content-type", "application/json; charset=utf-8"], ["X-Test", "post"]],
            },
            {
                name: "response Headers-like object",
                contentType: "application/json",
                headers: async () => (await fetch("data:application/json,")).headers,
            },
        ];

        for (const entry of initializers) {
            it(`fetch sends JSON with ${entry.name} and consumes the response`, async function () {
                const response = await fetch(`${httpTestUrl}/echo`, {
                    method: "POST",
                    headers: await entry.headers(),
                    body,
                });
                expect(response.status).to.equal(200);
                expect(response.ok).to.equal(true);
                expect(response.headers.get("x-request-method")).to.equal("POST");
                expect(response.headers.get("x-request-content-type")).to.equal(entry.contentType);
                expect(response.headers.get("x-request-body-length")).to.equal(String(bytes(body).length));
                if (entry.name !== "response Headers-like object") {
                    expect(response.headers.get("x-request-test")).to.equal("post");
                }
                expect(await response.clone().json()).to.eql(JSON.parse(body));
                expect(await response.clone().text()).to.equal(body);
                expect(Array.from(new Uint8Array(await response.clone().arrayBuffer()))).to.eql(bytes(body));
                expect(await (await response.blob()).text()).to.equal(body);
            });
        }

        for (const name of ["Content-Type", "content-type", "CoNtEnT-TyPe"]) {
            it(`XHR sends JSON with ${name} and preserves MIME parameters`, async function () {
                const { xhr, errors } = await sendXhr(`${httpTestUrl}/echo`, {
                    method: "POST", body, responseType: "text",
                    headers: { [name]: "application/json; charset=utf-8", "X-Test": "post" },
                });
                expect(errors).to.equal(0);
                expect(xhr.readyState).to.equal(4);
                expect(xhr.status).to.equal(200);
                expect(xhr.getResponseHeader("x-request-method")).to.equal("POST");
                expect(xhr.getResponseHeader("x-request-content-type")).to.equal("application/json; charset=utf-8");
                expect(xhr.getResponseHeader("x-request-test")).to.equal("post");
                expect(xhr.getResponseHeader("x-request-body-length")).to.equal(String(bytes(body).length));
                expect(xhr.responseText).to.equal(body);
                expect(JSON.parse(xhr.responseText)).to.eql(JSON.parse(body));
            });
        }

        for (const entry of [
            { name: "empty body", body: "" },
            { name: "UTF-8 and embedded NUL bytes", body: "\0start\0caf\u00e9 \u20ac\r\nend\0" },
        ]) {
            for (const contentType of [undefined, "application/octet-stream"]) {
                const label = `${entry.name}, ${contentType ? "explicit" : "omitted"} Content-Type`;
                it(`fetch preserves ${label}`, async function () {
                    const response = await fetch(`${httpTestUrl}/echo`, {
                        method: "POST", body: entry.body,
                        headers: contentType ? { "content-type": contentType } : undefined,
                    });
                    expect(response.status).to.equal(200);
                    expect(response.headers.get("x-request-body-length")).to.equal(String(bytes(entry.body).length));
                    if (contentType || hostPlatform === "Win32" || hostPlatform === "UWP") {
                        expect(response.headers.get("x-request-content-type")).to.equal(contentType ?? "<absent>");
                    }
                    expect(Array.from(new Uint8Array(await response.arrayBuffer()))).to.eql(bytes(entry.body));
                });

                it(`XHR preserves ${label}`, async function () {
                    const { xhr, errors } = await sendXhr(`${httpTestUrl}/echo`, {
                        method: "POST", body: entry.body,
                        headers: contentType ? { "content-type": contentType } : undefined,
                    });
                    expect(errors).to.equal(0);
                    expect(xhr.status).to.equal(200);
                    expect(xhr.getResponseHeader("x-request-body-length")).to.equal(String(bytes(entry.body).length));
                    if (contentType || hostPlatform === "Win32" || hostPlatform === "UWP") {
                        expect(xhr.getResponseHeader("x-request-content-type")).to.equal(contentType ?? "<absent>");
                    }
                    expect(Array.from(new Uint8Array(xhr.response))).to.eql(bytes(entry.body));
                });
            }
        }

        if (hostPlatform === "Win32" || hostPlatform === "UWP") {
            it("rejects invalid MIME without retaining it in a reopened XHR", async function () {
                let error: unknown;
                try {
                    await fetch(`${httpTestUrl}/echo`, {
                        method: "POST", body,
                        headers: { "content-type": "not a media type" },
                    });
                } catch (caught) {
                    error = caught;
                }
                expect(error).to.be.instanceOf(TypeError);
                expect(error).to.have.nested.property("cause.status", 0);

                const failed = await sendXhr(`${httpTestUrl}/echo`, {
                    method: "POST", body, headers: { "content-type": "not a media type" },
                });
                expect(failed.errors).to.equal(1);
                expect(failed.xhr.status).to.equal(0);
                expect(failed.xhr.errorDetail).not.to.equal("");
                expect(failed.xhr.response.byteLength).to.equal(0);
                expect(failed.xhr.getResponseHeader("content-type")).to.equal(null);

                const reopened = await sendXhr(`${httpTestUrl}/echo`, {
                    xhr: failed.xhr, method: "POST", body,
                });
                expect(reopened.errors).to.equal(0);
                expect(reopened.xhr.status).to.equal(200);
                expect(reopened.xhr.errorDetail).to.equal("");
                expect(reopened.xhr.getResponseHeader("x-request-content-type")).to.equal("<absent>");
                expect(Array.from(new Uint8Array(reopened.xhr.response))).to.eql(bytes(body));
            });
        }
    });

    describe("missing response Content-Type", function () {
        for (const entry of [
            { path: "/no-content-type", body: '{"message":"hello"}', status: 200 },
            { path: "/empty", body: "", status: 200 },
            { path: "/no-content", body: "", status: hostPlatform === "Win32" || hostPlatform === "UWP" ? 200 : 204 },
        ]) {
            it(`fetch consumes ${entry.path}`, async function () {
                const response = await fetch(httpTestUrl + entry.path);
                expect(response.status).to.equal(entry.status);
                expect(response.ok).to.equal(true);
                expect(response.headers.has("content-type")).to.equal(false);
                expect(response.headers.get("Content-Type")).to.equal(null);
                expect(await response.clone().text()).to.equal(entry.body);
                if (entry.body) {
                    expect(await response.clone().json()).to.eql({ message: "hello" });
                }
                expect(Array.from(new Uint8Array(await response.clone().arrayBuffer()))).to.eql(bytes(entry.body));
                const blob = await response.blob();
                expect(blob.type).to.equal("");
                expect(await blob.text()).to.equal(entry.body);
            });

            for (const responseType of ["text", "arraybuffer"] as const) {
                it(`XHR consumes ${entry.path} as ${responseType}`, async function () {
                    const { xhr, errors } = await sendXhr(httpTestUrl + entry.path, { responseType });
                    expect(errors).to.equal(0);
                    expect(xhr.readyState).to.equal(4);
                    expect(xhr.status).to.equal(entry.status);
                    expect(xhr.getResponseHeader("content-type")).to.equal(null);
                    if (responseType === "text") {
                        expect(xhr.responseText).to.equal(entry.body);
                    } else {
                        expect(Array.from(new Uint8Array(xhr.response))).to.eql(bytes(entry.body));
                    }
                });
            }
        }
    });

    for (const entry of [
        { path: "/not-found", status: 404 },
        { path: "/server-error", status: 500 },
        { path: "/not-modified", status: 304 },
    ]) {
        it(`preserves HTTP ${entry.status} rather than treating it as a transport failure`, async function () {
            const response = await fetch(httpTestUrl + entry.path);
            expect(response.status).to.equal(entry.status);
            expect(response.ok).to.equal(false);
            expect(await response.text()).to.equal("");
            const { xhr, errors } = await sendXhr(httpTestUrl + entry.path);
            expect(xhr.status).to.equal(entry.status);
            expect(xhr.readyState).to.equal(4);
            expect(errors).to.equal(1);
        });
    }
});
