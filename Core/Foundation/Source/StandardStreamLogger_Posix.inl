// Shared POSIX redirection body for Android and Apple.
// The including TU must define SSL_WRITE_PLATFORM(stream, line) before include.

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace
{
    enum class Stream
    {
        Output,
        Error,
    };

    struct Channel
    {
        int Target{-1};
        int Original{-1};
        std::future<void> Completion{};
        std::thread Reader{};
    };

    std::vector<std::future<void>> g_outstandingDrains{};
    Channel g_stdout{};
    Channel g_stderr{};

    int Duplicate(int fd)
    {
        return ::dup(fd);
    }

    int DuplicateTo(int source, int target)
    {
        return ::dup2(source, target) < 0 ? -1 : 0;
    }

    int Close(int fd)
    {
        return ::close(fd);
    }

    int64_t Read(int fd, void* data, size_t size)
    {
        return ::read(fd, data, size);
    }

    int64_t Write(int fd, const void* data, size_t size)
    {
        return ::write(fd, data, size);
    }

    int CreatePipe(int fds[2])
    {
        if (::pipe(fds) != 0)
        {
            return -1;
        }
        // Mark both ends CLOEXEC. Leaving the write end inheritable would let a
        // concurrent exec keep the pipe open and delay Drain()'s EOF on Stop().
        if (::fcntl(fds[0], F_SETFD, FD_CLOEXEC) != 0 ||
            ::fcntl(fds[1], F_SETFD, FD_CLOEXEC) != 0)
        {
            const int error = errno;
            (void)::close(fds[0]);
            (void)::close(fds[1]);
            errno = error;
            return -1;
        }
        return 0;
    }

    void WritePlatform(Stream stream, const std::string& line)
    {
        SSL_WRITE_PLATFORM(stream, line);
    }

    bool WriteAll(int fd, const char* data, size_t size)
    {
        while (size != 0)
        {
            const auto written = Write(fd, data, size);
            if (written > 0)
            {
                data += written;
                size -= static_cast<size_t>(written);
                continue;
            }
            if (written < 0 && errno == EINTR)
            {
                continue;
            }
            return false;
        }
        return true;
    }

    void EmitLine(Stream stream, std::string line)
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        WritePlatform(stream, line);
    }

    void Drain(int readFd, int originalFd, Stream stream)
    {
        // Cap mirrored lines below typical platform limits:
        // Android logcat (~4 KiB) and Apple os_log payloads. Leave headroom under 4096.
        constexpr size_t MAX_PLATFORM_LINE_SIZE{3800};
        std::array<char, 1024> buffer{};
        std::string pending{};

        for (;;)
        {
            const auto count = Read(readFd, buffer.data(), buffer.size());
            if (count == 0)
            {
                break;
            }
            if (count < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }

            const size_t size = static_cast<size_t>(count);
            if (originalFd >= 0)
            {
                (void)WriteAll(originalFd, buffer.data(), size);
            }

            pending.append(buffer.data(), size);

            size_t start = 0;
            for (;;)
            {
                const size_t newline = pending.find('\n', start);
                if (newline != std::string::npos)
                {
                    EmitLine(stream, pending.substr(start, newline - start));
                    start = newline + 1;
                }
                else if (pending.size() - start >= MAX_PLATFORM_LINE_SIZE)
                {
                    EmitLine(stream, pending.substr(start, MAX_PLATFORM_LINE_SIZE));
                    start += MAX_PLATFORM_LINE_SIZE;
                }
                else
                {
                    break;
                }
            }
            if (start != 0)
            {
                pending.erase(0, start);
            }
        }

        if (!pending.empty())
        {
            EmitLine(stream, std::move(pending));
        }
        (void)Close(readFd);
        if (originalFd >= 0)
        {
            (void)Close(originalFd);
        }
    }

    bool OccupyTarget(int target)
    {
        const int nullFd = ::open("/dev/null", O_WRONLY);
        if (nullFd < 0)
        {
            return false;
        }
        if (nullFd == target)
        {
            return true;
        }

        const bool duplicated = DuplicateTo(nullFd, target) == 0;
        (void)Close(nullFd);
        return duplicated;
    }

    bool StartChannel(Channel& channel, int target, Stream stream)
    {
        channel.Target = target;
        errno = 0;
        channel.Original = Duplicate(target);
        if (channel.Original < 0 && errno != EBADF)
        {
            channel = {};
            return false;
        }
        if (channel.Original < 0 && !OccupyTarget(target))
        {
            channel = {};
            return false;
        }

        int pipeFds[2]{-1, -1};
        if (CreatePipe(pipeFds) != 0)
        {
            if (channel.Original >= 0)
            {
                (void)Close(channel.Original);
            }
            else
            {
                (void)Close(target);
            }
            channel = {};
            return false;
        }

        if (DuplicateTo(pipeFds[1], target) != 0)
        {
            (void)Close(pipeFds[0]);
            (void)Close(pipeFds[1]);
            if (channel.Original >= 0)
            {
                (void)Close(channel.Original);
            }
            else
            {
                (void)Close(target);
            }
            channel = {};
            return false;
        }
        (void)Close(pipeFds[1]);

        int readerOriginal{-1};
        if (channel.Original >= 0)
        {
            readerOriginal = Duplicate(channel.Original);
            if (readerOriginal < 0)
            {
                (void)DuplicateTo(channel.Original, target);
                (void)Close(channel.Original);
                (void)Close(pipeFds[0]);
                channel = {};
                return false;
            }
        }

        std::promise<void> completed{};
        channel.Completion = completed.get_future();
        try
        {
            channel.Reader = std::thread{
                [readFd = pipeFds[0], originalFd = readerOriginal, stream, completed = std::move(completed)]() mutable {
                    Drain(readFd, originalFd, stream);
                    completed.set_value();
                }};
        }
        catch (const std::system_error&)
        {
            if (channel.Original >= 0)
            {
                (void)DuplicateTo(channel.Original, target);
                (void)Close(channel.Original);
            }
            else
            {
                (void)Close(target);
            }
            (void)Close(pipeFds[0]);
            if (readerOriginal >= 0)
            {
                (void)Close(readerOriginal);
            }
            channel = {};
            return false;
        }
        return true;
    }

    bool StopChannel(Channel& channel)
    {
        bool restored{true};
        if (channel.Original >= 0)
        {
            restored = DuplicateTo(channel.Original, channel.Target) == 0;
            if (!restored)
            {
                (void)Close(channel.Target);
            }
        }
        else
        {
            restored = Close(channel.Target) == 0;
        }

        if (channel.Original >= 0)
        {
            (void)Close(channel.Original);
        }

        if (channel.Reader.joinable())
        {
            if (channel.Completion.wait_for(std::chrono::seconds{2}) == std::future_status::ready)
            {
                channel.Reader.join();
            }
            else
            {
                g_outstandingDrains.push_back(std::move(channel.Completion));
                channel.Reader.detach();
                restored = false;
            }
        }
        channel = {};
        return restored;
    }

    bool ReapOutstandingDrains(std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!g_outstandingDrains.empty())
        {
            auto& front = g_outstandingDrains.front();
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds::zero())
            {
                if (front.wait_for(std::chrono::milliseconds::zero()) != std::future_status::ready)
                {
                    return false;
                }
            }
            else if (front.wait_for(remaining) != std::future_status::ready)
            {
                return false;
            }

            g_outstandingDrains.erase(g_outstandingDrains.begin());
        }
        return true;
    }
}

namespace Babylon::StandardStreamLogger::Platform
{
    bool Start()
    {
        if (!ReapOutstandingDrains(std::chrono::seconds{2}))
        {
            return false;
        }

        std::cout.flush();
        std::cerr.flush();
        std::fflush(stdout);
        std::fflush(stderr);

        if (!StartChannel(g_stdout, 1, Stream::Output))
        {
            return false;
        }
        if (!StartChannel(g_stderr, 2, Stream::Error))
        {
            (void)StopChannel(g_stdout);
            return false;
        }
        return true;
    }

    bool Stop()
    {
        std::cout.flush();
        std::cerr.flush();
        std::fflush(stdout);
        std::fflush(stderr);
        bool stopped = StopChannel(g_stdout);
        stopped = StopChannel(g_stderr) && stopped;
        return stopped;
    }
}