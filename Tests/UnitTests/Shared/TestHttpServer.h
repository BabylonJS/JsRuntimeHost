#pragma once

#include <memory>
#include <string>

namespace Babylon::Test
{
    // A loopback HTTP/1.1 server the JavaScript network tests (XMLHttpRequest, fetch) talk to
    // instead of public hosts, so a run never depends on github.com answering -- or on the CI
    // runner having outbound network at all. The request-target is percent-decoded before routing:
    //
    //   /                       200 "OK"
    //   /assets/<name>          200 when <name> decodes to the expected UTF-8 file name (so the
    //                           client's own percent-encoding is what gets exercised), else 404
    //   /delay/<milliseconds>   200 after the delay, for the abort-in-flight tests
    //   anything else           404 "Not Found"
    //
    // Every response is text/plain with Connection: close. The port is ephemeral; scripts reach the
    // server through the `hostTestServer` global the host sets to Origin().
    class TestHttpServer
    {
    public:
        TestHttpServer();
        ~TestHttpServer();

        TestHttpServer(const TestHttpServer&) = delete;
        TestHttpServer& operator=(const TestHttpServer&) = delete;

        // "http://127.0.0.1:<port>", no trailing slash.
        const std::string& Origin() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
