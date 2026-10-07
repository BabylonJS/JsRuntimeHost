import { expect } from "chai";
import { bytes, sendXhr } from "./httpTestHelpers";

describe("UrlLib data URLs", function () {
    this.timeout(15000);

    const cases = [
        { name: "percent-encoded default MIME", url: "data:,hello%20world", body: bytes("hello world"), mime: "text/plain;charset=US-ASCII" },
        { name: "empty body", url: "data:,", body: [], mime: "text/plain;charset=US-ASCII" },
        { name: "empty base64", url: "data:;base64,", body: [], mime: "text/plain;charset=US-ASCII" },
        { name: "UTF-8 and case-insensitive scheme", url: "DATA:TEXT/PLAIN;CHARSET=UTF-8,%C3%A9", body: bytes("\u00e9"), mime: "text/plain;charset=UTF-8" },
        { name: "charset without media type", url: "data:;charset=utf-8,%E2%82%AC", body: bytes("\u20ac"), mime: "text/plain;charset=utf-8" },
        { name: "binary percent escapes", url: "data:application/octet-stream,%00%7f%80%ff", body: [0, 127, 128, 255], mime: "application/octet-stream" },
        { name: "PNG signature", url: "data:image/png;base64,iVBORw0KGgo=", body: [137, 80, 78, 71, 13, 10, 26, 10], mime: "image/png" },
        { name: "unpadded base64", url: "data:;BASE64,Zm8", body: bytes("fo"), mime: "text/plain;charset=US-ASCII" },
        { name: "forgiving base64 pad bits", url: "data:;base64,Zh==", body: bytes("f"), mime: "text/plain;charset=US-ASCII" },
        { name: "escaped base64 whitespace and padding", url: "data:;base64,Z%20g%3D%3D%0D%0A%09%0C", body: bytes("f"), mime: "text/plain;charset=US-ASCII" },
        { name: "standard base64 alphabet", url: "data:application/octet-stream;base64,%2F%2B%2F%2B", body: [255, 239, 254], mime: "application/octet-stream" },
        { name: "query bytes and excluded fragment", url: "data:text/plain,x?y#ignored", body: bytes("x?y"), mime: "text/plain" },
        { name: "literal plus, commas, and escaped delimiters", url: "data:text/plain,a+b,c%23%3F%25", body: bytes("a+b,c#?%"), mime: "text/plain" },
        { name: "literal malformed percent escapes", url: "data:text/plain,%oops%2%", body: bytes("%oops%2%"), mime: "text/plain" },
        { name: "quoted MIME parameter", url: 'data:text/plain;note="a;b",x', body: bytes("x"), mime: 'text/plain;note="a;b"' },
        { name: "base64 parameter is not an encoding marker", url: "data:text/plain;base64=value,Zg==", body: bytes("Zg=="), mime: "text/plain;base64=value" },
        { name: "charset does not transcode raw bytes", url: "data:text/plain;charset=iso-8859-1,%E9", body: [233], mime: "text/plain;charset=iso-8859-1" },
    ];

    for (const entry of cases) {
        it(`fetch decodes ${entry.name}`, async function () {
            const response = await fetch(entry.url);
            expect(response.status).to.equal(200);
            expect(response.statusText).to.equal("OK");
            expect(response.ok).to.equal(true);
            expect(response.url).to.equal(entry.url.split("#")[0]);
            expect(response.headers.get("CONTENT-TYPE")).to.equal(entry.mime);
            expect(Array.from(new Uint8Array(await response.clone().arrayBuffer()))).to.eql(entry.body);
            const blob = await response.blob();
            expect(blob.type).to.equal(entry.mime);
            expect(Array.from(new Uint8Array(await blob.arrayBuffer()))).to.eql(entry.body);
        });

        it(`XHR decodes ${entry.name}`, async function () {
            const { xhr, errors } = await sendXhr(entry.url);
            expect(errors).to.equal(0);
            expect(xhr.readyState).to.equal(4);
            expect(xhr.status).to.equal(200);
            expect(xhr.statusText).to.equal("OK");
            expect(xhr.responseURL).to.equal(entry.url.split("#")[0]);
            expect(xhr.getResponseHeader("Content-Type")).to.equal(entry.mime);
            expect(xhr.errorCode).to.equal("");
            expect(Array.from(new Uint8Array(xhr.response))).to.eql(entry.body);
        });
    }

    it("preserves every byte value through Fetch and XHR", async function () {
        const encoded =
            "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4v" +
            "MDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5f" +
            "YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6P" +
            "kJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/" +
            "wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v" +
            "8PHy8/T19vf4+fr7/P3+/w==";
        const expected = Array.from({ length: 256 }, (_, index) => index);
        const url = `data:application/octet-stream;base64,${encoded}`;
        const response = await fetch(url);
        expect(Array.from(new Uint8Array(await response.arrayBuffer()))).to.eql(expected);
        const { xhr, errors } = await sendXhr(url);
        expect(errors).to.equal(0);
        expect(Array.from(new Uint8Array(xhr.response))).to.eql(expected);
    });

    it("exposes UTF-8 text, embedded NULs, and parsed JSON", async function () {
        const text = "start\0caf\u00e9\0end";
        const url = `data:text/plain;charset=utf-8,${encodeURIComponent(text)}`;
        expect(await (await fetch(url)).text()).to.equal(text);
        const { xhr, errors } = await sendXhr(url, { responseType: "text" });
        expect(errors).to.equal(0);
        expect(xhr.responseText).to.equal(text);
        expect(xhr.response).to.equal(text);
        const value = { text, nested: [1, true] };
        const jsonUrl = `data:application/json,${encodeURIComponent(JSON.stringify(value))}`;
        expect(await (await fetch(jsonUrl)).json()).to.eql(value);
    });

    for (const url of [
        "data:", "data:text/plain#fragment,not-body", "data:invalid,x",
        "data:text/plain;broken,x", 'data:text/plain;charset="broken,x',
        "data:;base64,A", "data:;base64,Zg=", "data:;base64,Zg===",
        "data:;base64,Zm=8", "data:;base64,Zg==junk", "data:;base64,_w==",
        "data:;base64,%FF", "data:;base64,Zg==?query",
    ]) {
        it(`rejects malformed input without a partial response: ${url}`, async function () {
            let error: unknown;
            try {
                await fetch(url);
            } catch (caught) {
                error = caught;
            }
            expect(error).to.be.instanceOf(TypeError);
            expect(error).to.have.property("message", "fetch failed");
            expect(error).to.have.nested.property("cause.status", 0);
            expect(error).to.have.nested.property("cause.url", url);
            expect(error).to.have.nested.property("cause.code", "DataUrlInvalid");
            const { xhr, errors } = await sendXhr(url);
            expect(errors).to.equal(1);
            expect(xhr.readyState).to.equal(4);
            expect(xhr.status).to.equal(0);
            expect(xhr.statusText).to.equal("");
            expect(xhr.errorCode).to.equal("DataUrlInvalid");
            expect(xhr.errorDetail).not.to.equal("");
            expect(xhr.response.byteLength).to.equal(0);
            expect(xhr.getResponseHeader("content-type")).to.equal(null);
        });
    }

    it("rejects POST rather than silently decoding a GET response", async function () {
        let error: unknown;
        try {
            await fetch("data:,hello", { method: "POST", body: "ignored" });
        } catch (caught) {
            error = caught;
        }
        expect(error).to.be.instanceOf(TypeError);
        expect(error).to.have.nested.property("cause.status", 0);
        expect(error).to.have.nested.property("cause.code", "DataUrlUnsupportedMethod");
        const { xhr, errors } = await sendXhr("data:,hello", { method: "POST", body: "ignored" });
        expect(errors).to.equal(1);
        expect(xhr.status).to.equal(0);
        expect(xhr.errorCode).to.equal("DataUrlUnsupportedMethod");
        expect(xhr.response.byteLength).to.equal(0);
    });

    for (const abortBeforeFetch of [true, false]) {
        it(`honors Fetch abort ${abortBeforeFetch ? "before" : "immediately after"} sending`, async function () {
            const controller = new AbortController();
            if (abortBeforeFetch) {
                controller.abort();
            }
            const pending = fetch("data:,hello", { signal: controller.signal });
            controller.abort();
            let error: unknown;
            try {
                await pending;
            } catch (caught) {
                error = caught;
            }
            expect(error).to.have.property("name", "AbortError");
        });
    }

    it("honors XHR cancellation before shared resolution", async function () {
        const { xhr, errors } = await sendXhr("data:,hello", { abortBeforeSend: true });
        expect(errors).to.equal(1);
        expect(xhr.status).to.equal(0);
        expect(xhr.response.byteLength).to.equal(0);
        expect(xhr.getResponseHeader("content-type")).to.equal(null);
    });

    it("clears XHR errors, bodies, and headers when switching transports", async function () {
        const first = await sendXhr("data:;base64,!!!");
        expect(first.errors).to.equal(1);
        expect(first.xhr.errorCode).to.equal("DataUrlInvalid");
        const xhr = first.xhr;
        for (const entry of [
            { url: "data:text/plain,first", body: "first", mime: "text/plain" },
            { url: `${httpTestUrl}/no-content-type`, body: '{"message":"hello"}', mime: null },
            { url: "data:,", body: "", mime: "text/plain;charset=US-ASCII" },
        ]) {
            const result = await sendXhr(entry.url, { xhr });
            expect(result.errors).to.equal(0);
            expect(xhr.status).to.equal(200);
            expect(xhr.errorCode).to.equal("");
            expect(xhr.errorDetail).to.equal("");
            expect(xhr.getResponseHeader("content-type")).to.equal(entry.mime);
            expect(Array.from(new Uint8Array(xhr.response))).to.eql(bytes(entry.body));
        }
    });
});
