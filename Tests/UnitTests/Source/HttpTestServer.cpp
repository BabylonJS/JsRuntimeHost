#include "HttpTestServer.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <exception>
#include <map>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace
{
#if defined(_WIN32)
    using NativeSocket = SOCKET;
    using SocketLength = int;
    constexpr auto InvalidSocket = INVALID_SOCKET;
#else
    using NativeSocket = int;
    using SocketLength = socklen_t;
    constexpr auto InvalidSocket = -1;
#endif

    struct Socket
    {
        NativeSocket Value;

        explicit Socket(NativeSocket value)
            : Value{value}
        {
        }
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        ~Socket()
        {
            if (Value != InvalidSocket)
            {
#if defined(_WIN32)
                ::closesocket(Value);
#else
                ::close(Value);
#endif
            }
        }
    };

    std::string Lowercase(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool Wait(NativeSocket socket, bool writing, const std::atomic<bool>& stop,
        std::chrono::steady_clock::time_point deadline)
    {
        while (!stop.load())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                throw std::runtime_error{"HTTP test server: connection timed out"};
            }
            fd_set descriptors;
            FD_ZERO(&descriptors);
            FD_SET(socket, &descriptors);
            timeval timeout{0, 100000};
            const auto ready = ::select(
#if defined(_WIN32)
                0,
#else
                socket + 1,
#endif
                writing ? nullptr : &descriptors, writing ? &descriptors : nullptr, nullptr, &timeout);
            if (ready < 0)
            {
                throw std::runtime_error{"HTTP test server: select failed"};
            }
            if (ready > 0)
            {
                return true;
            }
        }
        return false;
    }

    void Send(NativeSocket socket, std::string_view bytes, const std::atomic<bool>& stop)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!bytes.empty() && Wait(socket, true, stop, deadline))
        {
            const auto count = ::send(socket, bytes.data(), static_cast<int>(bytes.size()),
#if defined(MSG_NOSIGNAL)
                MSG_NOSIGNAL
#else
                0
#endif
            );
            if (count <= 0)
            {
                throw std::runtime_error{"HTTP test server: send failed"};
            }
            bytes.remove_prefix(static_cast<size_t>(count));
        }
    }

    void Respond(NativeSocket socket, const std::atomic<bool>& stop)
    {
#if defined(_WIN32)
        const DWORD timeout = 1000;
#else
        const timeval timeout{1, 0};
#endif
        if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0 ||
            ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0)
        {
            throw std::runtime_error{"HTTP test server: could not set socket timeouts"};
        }
#if defined(__APPLE__)
        const int noSignal = 1;
        if (::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal)) != 0)
        {
            throw std::runtime_error{"HTTP test server: SO_NOSIGPIPE failed"};
        }
#endif
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        std::string request;
        size_t bodyStart = std::string::npos;
        size_t bodySize{};
        std::map<std::string, std::string> headers;
        while (Wait(socket, false, stop, deadline))
        {
            std::array<char, 4096> buffer;
            const auto count = ::recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (count <= 0)
            {
                throw std::runtime_error{"HTTP test server: incomplete request"};
            }
            request.append(buffer.data(), static_cast<size_t>(count));
            if (request.size() > 65536)
            {
                throw std::runtime_error{"HTTP test server: request exceeds test fixture limit"};
            }
            if (bodyStart == std::string::npos)
            {
                const auto end = request.find("\r\n\r\n");
                if (end == std::string::npos)
                {
                    continue;
                }
                bodyStart = end + 4;
                for (auto line = request.find("\r\n") + 2; line < end;)
                {
                    const auto next = request.find("\r\n", line);
                    const auto colon = request.find(':', line);
                    if (colon == std::string::npos || colon >= next)
                    {
                        throw std::runtime_error{"HTTP test server: malformed request header"};
                    }
                    const auto value = request.find_first_not_of(" \t", colon + 1);
                    headers.emplace(Lowercase(request.substr(line, colon - line)),
                        value < next ? request.substr(value, next - value) : "");
                    line = next + 2;
                }
                if (headers.contains("transfer-encoding"))
                {
                    throw std::runtime_error{"HTTP test server expects length-delimited string bodies"};
                }
                if (const auto length = headers.find("content-length"); length != headers.end())
                {
                    const auto& value = length->second;
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), bodySize);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || bodySize > 32768)
                    {
                        throw std::runtime_error{"HTTP test server: invalid Content-Length"};
                    }
                }
                if (const auto expect = headers.find("expect");
                    expect != headers.end() && Lowercase(expect->second) == "100-continue")
                {
                    Send(socket, "HTTP/1.1 100 Continue\r\n\r\n", stop);
                }
            }
            if (request.size() >= bodyStart + bodySize)
            {
                break;
            }
        }
        if (stop.load())
        {
            return;
        }

        const auto methodEnd = request.find(' ');
        const auto pathEnd = request.find(' ', methodEnd + 1);
        if (methodEnd == std::string::npos || pathEnd == std::string::npos)
        {
            throw std::runtime_error{"HTTP test server: malformed request line"};
        }
        const auto method = request.substr(0, methodEnd);
        const auto path = request.substr(methodEnd + 1, pathEnd - methodEnd - 1);
        std::string status{"200 OK"};
        std::string body;
        std::string responseHeaders;
        if (path == "/echo")
        {
            body = request.substr(bodyStart, bodySize);
            responseHeaders = "Content-Type: text/plain; charset=utf-8\r\n";
            responseHeaders += "x-request-method: " + method + "\r\n";
            responseHeaders += "x-request-body-length: " + std::to_string(body.size()) + "\r\n";
            responseHeaders += "x-request-content-type: " + (headers.contains("content-type") ? headers.at("content-type") : "<absent>") + "\r\n";
            responseHeaders += "x-request-test: " + (headers.contains("x-test") ? headers.at("x-test") : "<absent>") + "\r\n";
        }
        else if (path == "/no-content-type")
        {
            body = "{\"message\":\"hello\"}";
        }
        else if (path == "/empty")
        {
        }
        else if (path == "/no-content")
        {
            status = "204 No Content";
        }
        else if (path == "/not-found")
        {
            status = "404 Not Found";
        }
        else if (path == "/server-error")
        {
            status = "500 Internal Server Error";
        }
        else if (path == "/not-modified")
        {
            status = "304 Not Modified";
        }
        else
        {
            throw std::runtime_error{"HTTP test server: unexpected path " + path};
        }

        Send(socket, "HTTP/1.1 " + status + "\r\n" + responseHeaders + "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body, stop);
    }
}

struct HttpTestServer::Impl
{
#if defined(_WIN32)
    struct Winsock
    {
        Winsock()
        {
            WSADATA data{};
            if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
            {
                throw std::runtime_error{"HTTP test server: WSAStartup failed"};
            }
        }
        ~Winsock() { ::WSACleanup(); }
    } Sockets;
#endif
    Socket Listener{::socket(AF_INET, SOCK_STREAM, 0)};
    uint16_t Port{};
    std::exception_ptr Error;
    std::atomic<bool> Stopping{};
    std::thread Worker;

    void Stop()
    {
        Stopping.store(true);
        if (Worker.joinable())
        {
            Worker.join();
        }
    }

    ~Impl() { Stop(); }

    Impl()
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        SocketLength length = sizeof(address);
        if (Listener.Value == InvalidSocket ||
            ::bind(Listener.Value, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
            ::getsockname(Listener.Value, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
            ::listen(Listener.Value, 8) != 0)
        {
            throw std::runtime_error{"HTTP test server: could not listen on loopback"};
        }
        Port = ntohs(address.sin_port);
        Worker = std::thread{[this] {
            try
            {
                while (Wait(Listener.Value, false, Stopping, (std::chrono::steady_clock::time_point::max)()))
                {
                    const Socket connection{::accept(Listener.Value, nullptr, nullptr)};
                    if (connection.Value == InvalidSocket)
                    {
                        throw std::runtime_error{"HTTP test server: accept failed"};
                    }
                    Respond(connection.Value, Stopping);
                }
            }
            catch (...)
            {
                Error = std::current_exception();
            }
        }};
    }
};

HttpTestServer::HttpTestServer()
    : m_impl{std::make_unique<Impl>()}
{
}

HttpTestServer::~HttpTestServer() = default;

std::string HttpTestServer::Url() const
{
    return "http://127.0.0.1:" + std::to_string(m_impl->Port);
}

void HttpTestServer::Stop()
{
    m_impl->Stop();
    if (m_impl->Error)
    {
        std::rethrow_exception(m_impl->Error);
    }
}
