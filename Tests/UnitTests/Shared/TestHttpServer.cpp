#include "TestHttpServer.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Babylon::Test
{
    namespace
    {
#ifdef _WIN32
        using Socket = SOCKET;
        constexpr Socket InvalidSocket = INVALID_SOCKET;
        constexpr int SendFlags = 0;

        void CloseSocket(Socket socket)
        {
            closesocket(socket);
        }

        int WaitReadable(Socket socket, int timeoutMilliseconds)
        {
            WSAPOLLFD descriptor{socket, POLLRDNORM, 0};
            return WSAPoll(&descriptor, 1, timeoutMilliseconds);
        }
#else
        using Socket = int;
        constexpr Socket InvalidSocket = -1;
#ifdef MSG_NOSIGNAL
        // A client that aborted mid-request has closed its end; writing to it must not raise SIGPIPE.
        constexpr int SendFlags = MSG_NOSIGNAL;
#else
        constexpr int SendFlags = 0;
#endif

        void CloseSocket(Socket socket)
        {
            ::close(socket);
        }

        int WaitReadable(Socket socket, int timeoutMilliseconds)
        {
            pollfd descriptor{socket, POLLIN, 0};
            return ::poll(&descriptor, 1, timeoutMilliseconds);
        }
#endif

        // How long a connected client may stay silent before the connection is dropped.
        constexpr int ClientTimeoutMilliseconds = 5000;

        std::string PercentDecode(const std::string& text)
        {
            const auto hexValue = [](char character) -> int {
                if (character >= '0' && character <= '9')
                {
                    return character - '0';
                }
                if (character >= 'a' && character <= 'f')
                {
                    return character - 'a' + 10;
                }
                if (character >= 'A' && character <= 'F')
                {
                    return character - 'A' + 10;
                }
                return -1;
            };

            std::string result;
            result.reserve(text.size());
            for (size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '%' && index + 2 < text.size())
                {
                    const int high = hexValue(text[index + 1]);
                    const int low = hexValue(text[index + 2]);
                    if (high >= 0 && low >= 0)
                    {
                        result.push_back(static_cast<char>(high * 16 + low));
                        index += 2;
                        continue;
                    }
                }
                result.push_back(text[index]);
            }
            return result;
        }

        // The one asset the encoding tests request: "στρογγυλεμένος % κύβος.glb", spelled here in its
        // fully escaped form so the file's own encoding never matters.
        const std::string& ExpectedAssetPath()
        {
            static const std::string path =
                "/assets/" + PercentDecode("%CF%83%CF%84%CF%81%CE%BF%CE%B3%CE%B3%CF%85%CE%BB%CE%B5%CE%BC%CE%AD%CE%BD%CE%BF%CF%82"
                                           "%20%25%20%CE%BA%CF%8D%CE%B2%CE%BF%CF%82.glb");
            return path;
        }

        std::string ToLower(std::string text)
        {
            for (auto& character : text)
            {
                if (character >= 'A' && character <= 'Z')
                {
                    character = static_cast<char>(character - 'A' + 'a');
                }
            }
            return text;
        }

        // Reads the request head into `head` and drains the body announced by Content-Length, so the
        // peer never sees a reset before it has finished sending. Returns false when the client goes
        // away or stays silent.
        bool ReadRequest(Socket client, std::string& head)
        {
            std::string buffer;
            char chunk[4096];
            size_t headEnd = std::string::npos;
            while (headEnd == std::string::npos)
            {
                if (buffer.size() > 64 * 1024 || WaitReadable(client, ClientTimeoutMilliseconds) <= 0)
                {
                    return false;
                }
                const auto received = ::recv(client, chunk, static_cast<int>(sizeof(chunk)), 0);
                if (received <= 0)
                {
                    return false;
                }
                buffer.append(chunk, static_cast<size_t>(received));
                headEnd = buffer.find("\r\n\r\n");
            }
            head = buffer.substr(0, headEnd);

            size_t contentLength = 0;
            const std::string loweredHead = ToLower(head);
            const auto lengthHeader = loweredHead.find("\r\ncontent-length:");
            if (lengthHeader != std::string::npos)
            {
                contentLength = static_cast<size_t>(std::strtoull(loweredHead.c_str() + lengthHeader + 17, nullptr, 10));
            }
            size_t bodyReceived = buffer.size() - (headEnd + 4);
            while (bodyReceived < contentLength)
            {
                if (WaitReadable(client, ClientTimeoutMilliseconds) <= 0)
                {
                    return false;
                }
                const auto received = ::recv(client, chunk, static_cast<int>(sizeof(chunk)), 0);
                if (received <= 0)
                {
                    return false;
                }
                bodyReceived += static_cast<size_t>(received);
            }
            return true;
        }

        struct Response
        {
            int Status;
            const char* Reason;
            std::string Body;
        };

        Response Route(const std::string& target, const std::atomic<bool>& stopping)
        {
            std::string path = target;
            const auto query = path.find('?');
            if (query != std::string::npos)
            {
                path.erase(query);
            }
            path = PercentDecode(path);

            if (path == "/")
            {
                return {200, "OK", "OK"};
            }
            if (path == ExpectedAssetPath())
            {
                return {200, "OK", "glTF"};
            }
            const std::string delayPrefix = "/delay/";
            if (path.compare(0, delayPrefix.size(), delayPrefix) == 0)
            {
                const auto milliseconds = std::strtol(path.c_str() + delayPrefix.size(), nullptr, 10);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{milliseconds};
                while (!stopping && std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds{20});
                }
                return {200, "OK", "OK"};
            }
            return {404, "Not Found", "Not Found"};
        }

        void SendAll(Socket client, const std::string& data)
        {
            size_t sent = 0;
            while (sent < data.size())
            {
                const auto written = ::send(client, data.data() + sent, static_cast<int>(data.size() - sent), SendFlags);
                if (written <= 0)
                {
                    return;
                }
                sent += static_cast<size_t>(written);
            }
        }
    }

    struct TestHttpServer::Impl
    {
        Impl()
        {
#ifdef _WIN32
            WSADATA wsaData{};
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            {
                throw std::runtime_error{"TestHttpServer: WSAStartup failed"};
            }
#endif
            m_listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (m_listener == InvalidSocket)
            {
                throw std::runtime_error{"TestHttpServer: socket() failed"};
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0; // ephemeral
            if (::bind(m_listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || ::listen(m_listener, 16) != 0)
            {
                CloseSocket(m_listener);
                throw std::runtime_error{"TestHttpServer: bind/listen on 127.0.0.1 failed"};
            }

            sockaddr_in bound{};
            socklen_t boundLength = sizeof(bound);
            if (::getsockname(m_listener, reinterpret_cast<sockaddr*>(&bound), &boundLength) != 0)
            {
                CloseSocket(m_listener);
                throw std::runtime_error{"TestHttpServer: getsockname failed"};
            }
            Origin = "http://127.0.0.1:" + std::to_string(ntohs(bound.sin_port));

            m_acceptThread = std::thread{[this] { AcceptLoop(); }};
        }

        ~Impl()
        {
            m_stopping = true;
            if (m_acceptThread.joinable())
            {
                m_acceptThread.join();
            }
            for (auto& worker : m_workers)
            {
                worker.join();
            }
            CloseSocket(m_listener);
#ifdef _WIN32
            WSACleanup();
#endif
        }

        std::string Origin;

    private:
        void AcceptLoop()
        {
            while (!m_stopping)
            {
                if (WaitReadable(m_listener, 100) <= 0)
                {
                    continue;
                }
                const Socket client = ::accept(m_listener, nullptr, nullptr);
                if (client == InvalidSocket)
                {
                    continue;
                }
                std::lock_guard<std::mutex> lock{m_workersMutex};
                m_workers.emplace_back([this, client] { Serve(client); });
            }
        }

        void Serve(Socket client)
        {
#ifdef SO_NOSIGPIPE
            const int one = 1;
            ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
            std::string head;
            if (ReadRequest(client, head))
            {
                // request-line = method SP request-target SP HTTP-version
                const auto methodEnd = head.find(' ');
                const auto targetEnd = methodEnd == std::string::npos ? std::string::npos : head.find(' ', methodEnd + 1);
                Response response = targetEnd == std::string::npos
                    ? Response{400, "Bad Request", "Bad Request"}
                    : Route(head.substr(methodEnd + 1, targetEnd - methodEnd - 1), m_stopping);
                std::string message = "HTTP/1.1 " + std::to_string(response.Status) + " " + response.Reason + "\r\n" +
                                      "Content-Type: text/plain; charset=utf-8\r\n" +
                                      "Content-Length: " + std::to_string(response.Body.size()) + "\r\n" +
                                      "Connection: close\r\n\r\n";
                if (head.compare(0, 5, "HEAD ") != 0)
                {
                    message += response.Body;
                }
                SendAll(client, message);
            }
            CloseSocket(client);
        }

        Socket m_listener{InvalidSocket};
        std::atomic<bool> m_stopping{false};
        std::thread m_acceptThread;
        std::mutex m_workersMutex;
        std::vector<std::thread> m_workers;
    };

    TestHttpServer::TestHttpServer()
        : m_impl{std::make_unique<Impl>()}
    {
    }

    TestHttpServer::~TestHttpServer() = default;

    const std::string& TestHttpServer::Origin() const
    {
        return m_impl->Origin;
    }
}
