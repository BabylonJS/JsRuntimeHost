#include <Babylon/AppRuntime.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/ScriptLoader.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    class ResponseServer
    {
    public:
        ResponseServer(int status, bool truncated, bool streaming = false)
        {
            m_listener = socket(AF_INET, SOCK_STREAM, 0);
            if (m_listener == -1)
            {
                throw std::runtime_error{"Cannot create test HTTP socket"};
            }
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            socklen_t length = sizeof(address);
            if (bind(m_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
                getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
                listen(m_listener, 1) != 0)
            {
                close(m_listener);
                throw std::runtime_error{"Cannot bind test HTTP server"};
            }
            m_port = ntohs(address.sin_port);
            m_worker = std::thread{[this, status, truncated, streaming]() {
                const int connection = accept(m_listener, nullptr, nullptr);
                if (connection == -1)
                {
                    return;
                }
                timeval timeout{2, 0};
                setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#if defined(__APPLE__)
                int noSignal = 1;
                setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal));
#endif
                char request[4096];
                if (recv(connection, request, sizeof(request), 0) > 0)
                {
                    const std::string headers = "HTTP/1.1 " + std::to_string(status) +
                        (status == 404 ? " Not Found\r\n" : " OK\r\n") +
                        "Content-Length: " + (streaming ? "100000000" : truncated ? "100" : "5") +
                        "\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nhello";
                    const auto sendAll = [connection](const std::string& text) {
                        size_t offset{};
                        while (offset < text.size())
                        {
#if defined(MSG_NOSIGNAL)
                            constexpr int flags = MSG_NOSIGNAL;
#else
                            constexpr int flags = 0;
#endif
                            const auto sent = send(connection, text.data() + offset, text.size() - offset, flags);
                            if (sent <= 0)
                            {
                                return false;
                            }
                            offset += static_cast<size_t>(sent);
                        }
                        return true;
                    };
                    if (sendAll(headers))
                    {
                        m_started.set_value();
                        const std::string chunk(4096, 'x');
                        while (streaming && !m_stop && sendAll(chunk))
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                }
                shutdown(connection, SHUT_RDWR);
                close(connection);
            }};
        }

        ~ResponseServer()
        {
            m_stop = true;
            shutdown(m_listener, SHUT_RDWR);
            m_worker.join();
            close(m_listener);
        }

        std::string Url() const
        {
            return "http://127.0.0.1:" + std::to_string(m_port) + "/";
        }

        bool WaitForBody()
        {
            return m_started.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready;
        }

    private:
        int m_listener{};
        uint16_t m_port{};
        std::atomic<bool> m_stop{};
        std::promise<void> m_started;
        std::thread m_worker;
    };
}

TEST(XMLHttpRequest, CompletedAndTruncatedHttpBodies)
{
    for (const int status : {200, 404})
    {
        for (const bool truncated : {false, true})
        {
            SCOPED_TRACE(std::to_string(status) + (truncated ? " truncated" : " complete"));
            Babylon::AppRuntime runtime{};
            ResponseServer server{status, truncated};
            auto completed = std::make_shared<std::promise<std::string>>();
            auto result = completed->get_future();
            runtime.Dispatch([completed, url = server.Url()](Napi::Env env) {
                Babylon::Polyfills::XMLHttpRequest::Initialize(env);
                env.Global().Set("testUrl", url);
                env.Global().Set("report", Napi::Function::New(env, [completed](const Napi::CallbackInfo& info) {
                    completed->set_value(info[0].As<Napi::String>().Utf8Value());
                }));
                Napi::Eval(env, R"(
                    var xhr = new XMLHttpRequest();
                    var events = [];
                    xhr.onload = function () { events.push("load"); };
                    xhr.onerror = function () { events.push("error"); };
                    xhr.onloadend = function () {
                        events.push("loadend");
                        report(events.join(",") + ":" + xhr.status + ":" + xhr.responseText);
                    };
                    xhr.open("GET", testUrl);
                    xhr.send();
                )", "");
            });
            ASSERT_EQ(result.wait_for(std::chrono::seconds(10)), std::future_status::ready);
            EXPECT_EQ(result.get(), truncated ? "error,loadend:0:" : "load,loadend:" + std::to_string(status) + ":hello");
        }
    }
}

TEST(XMLHttpRequest, AbortResponseReadsWhileBodyIsStreaming)
{
    for (const char* responseType : {"text", "arraybuffer"})
    {
        SCOPED_TRACE(responseType);
        Babylon::AppRuntime runtime{};
        ResponseServer server{200, false, true};
        runtime.Dispatch([url = server.Url(), responseType](Napi::Env env) {
            Babylon::Polyfills::XMLHttpRequest::Initialize(env);
            env.Global().Set("testUrl", url);
            env.Global().Set("testResponseType", responseType);
            Napi::Eval(env, R"(
                var xhr = new XMLHttpRequest();
                xhr.open("GET", testUrl);
                xhr.responseType = testResponseType;
                xhr.send();
            )", "");
        });
        ASSERT_TRUE(server.WaitForBody());
        auto checked = std::make_shared<std::promise<bool>>();
        runtime.Dispatch([checked](Napi::Env env) {
            checked->set_value(Napi::Eval(env, R"(
                (function () {
                    var valid = true;
                    var calls = 0;
                    var read = function () {
                        ++calls;
                        valid = valid && xhr.response === (testResponseType === "text" ? "" : null) &&
                            xhr.responseText === "" && xhr.responseURL === "" &&
                            xhr.status === 0 && xhr.statusText === "" &&
                            xhr.errorCode === "" && xhr.errorDetail === "" &&
                            xhr.getResponseHeader("content-type") === null &&
                            Object.keys(xhr.getAllResponseHeaders()).length === 0;
                    };
                    for (var i = 0; i < 1000; ++i) read();
                    xhr.onreadystatechange = read;
                    xhr.onabort = read;
                    xhr.onloadend = read;
                    xhr.abort();
                    read();
                    return valid && calls === 1004 && xhr.readyState === XMLHttpRequest.UNSENT;
                })();
            )", "").As<Napi::Boolean>().Value());
        });
        auto result = checked->get_future();
        ASSERT_EQ(result.wait_for(std::chrono::seconds(10)), std::future_status::ready);
        EXPECT_TRUE(result.get());
    }
}
#endif

#if defined(JSRUNTIMEHOST_NAPI_ENGINE_CHAKRA)
#define USE_EDGEMODE_JSRT
#include <jsrt.h>

TEST(XMLHttpRequest, CollectsCompletedRequestWithSelfCapturingHandlers)
{
    auto finalized = std::make_shared<std::atomic<int>>(0);
    auto completed = std::make_shared<std::promise<void>>();
    auto completedFuture = completed->get_future();
    Babylon::AppRuntime runtime{};
    runtime.Dispatch([finalized, completed](Napi::Env env) {
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        env.Global().Set("observeXHR", Napi::Function::New(env, [finalized](const Napi::CallbackInfo& info) {
            auto marker = Napi::External<int>::New(info.Env(), new int{}, [finalized](Napi::Env, int* value) {
                delete value;
                ++*finalized;
            });
            info[0].As<Napi::Object>().Set("marker", marker);
        }));
        env.Global().Set("completedXHR", Napi::Function::New(env, [completed](const Napi::CallbackInfo&) {
            completed->set_value();
        }));
        Napi::Eval(env, R"(
            (function () {
                var xhr = new XMLHttpRequest();
                observeXHR(xhr);
                xhr.onload = function () { xhr.loaded = true; };
                xhr.addEventListener("loadend", function () {
                    if (xhr.loaded) completedXHR();
                });
                xhr.open("GET", "app:///Assets/symlink_target.js");
                xhr.send();
            })();
        )", "");
    });
    ASSERT_EQ(completedFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready);

    // Collect on later dispatches, after the completion continuation releases its anchor.
    for (int attempt = 0; attempt < 5 && finalized->load() == 0; ++attempt)
    {
        std::promise<void> collected;
        runtime.Dispatch([&collected](Napi::Env) {
            JsContextRef context{};
            JsRuntimeHandle engine{};
            EXPECT_EQ(JsGetCurrentContext(&context), JsNoError);
            EXPECT_EQ(JsGetRuntime(context, &engine), JsNoError);
            EXPECT_EQ(JsCollectGarbage(engine), JsNoError);
            collected.set_value();
        });
        collected.get_future().get();
    }
    EXPECT_EQ(finalized->load(), 1);
}
#endif

TEST(XMLHttpRequest, ThrowingStoppedListenerSkipsRemainingListeners)
{
    auto dispatched = std::make_shared<std::promise<std::string>>();
    auto reportedError = std::make_shared<std::promise<void>>();
    auto errorCount = std::make_shared<std::atomic<int>>(0);
    auto dispatchedFuture = dispatched->get_future();
    auto errorFuture = reportedError->get_future();

    Babylon::AppRuntime::Options options{};
    options.UnhandledExceptionHandler = [reportedError, errorCount](const Napi::Error&) {
        if (errorCount->fetch_add(1) == 0)
        {
            reportedError->set_value();
        }
    };

    Babylon::AppRuntime runtime{options};
    runtime.Dispatch([dispatched](Napi::Env env) {
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        env.Global().Set("reportStoppedListeners", Napi::Function::New(env, [dispatched](const Napi::CallbackInfo& info) {
            dispatched->set_value(info[0].As<Napi::String>().Utf8Value());
        }));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"(
        var xhr = new XMLHttpRequest();
        var invoked = [];
        xhr.addEventListener("load", function (event) {
            invoked.push("first");
            event.stopImmediatePropagation();
            throw new Error("stopped listener failed");
        });
        xhr.addEventListener("load", function () { invoked.push("second"); });
        xhr.addEventListener("loadend", function () {
            reportStoppedListeners(invoked.join(","));
        });
        xhr.open("GET", "app:///Assets/symlink_target.js");
        xhr.send();
    )", "");

    ASSERT_EQ(dispatchedFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(dispatchedFuture.get(), "first");
    ASSERT_EQ(errorFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(errorCount->load(), 1);

    auto drained = std::make_shared<std::promise<void>>();
    auto drainedFuture = drained->get_future();
    runtime.Dispatch([drained](Napi::Env) { drained->set_value(); });
    ASSERT_EQ(drainedFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready);
}
