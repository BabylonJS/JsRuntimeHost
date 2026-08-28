#include "StandardStreamLoggerPlatform.h"

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

#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>

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
        DWORD StandardHandle{};
        HANDLE OriginalHandle{INVALID_HANDLE_VALUE};
        bool OriginalHandleUsesTarget{};
        std::future<void> Completion{};
        std::thread Reader{};
    };

    // Drain futures retained after Stop() times out and detaches the reader.
    // Start() reaps these before installing a new redirection.
    std::vector<std::future<void>> g_outstandingDrains{};
    Channel g_stdout{};
    Channel g_stderr{};

    void IgnoreInvalidParameter(
        const wchar_t*,
        const wchar_t*,
        const wchar_t*,
        unsigned int,
        uintptr_t)
    {
    }

    int Duplicate(int fd)
    {
        return ::_dup(fd);
    }

    int DuplicateTo(int source, int target)
    {
        return ::_dup2(source, target);
    }

    int Close(int fd)
    {
        return ::_close(fd);
    }

    int64_t Read(int fd, void* data, size_t size)
    {
        return ::_read(fd, data, static_cast<unsigned int>(size));
    }

    int64_t Write(int fd, const void* data, size_t size)
    {
        return ::_write(fd, data, static_cast<unsigned int>(size));
    }

    int CreatePipe(int fds[2])
    {
        // UWP's CRT does not expose _pipe. CreatePipe + _open_osfhandle works on
        // desktop Win32 and UWP, and keeps the ends non-inheritable.
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = FALSE;

        HANDLE readHandle{INVALID_HANDLE_VALUE};
        HANDLE writeHandle{INVALID_HANDLE_VALUE};
        if (!::CreatePipe(&readHandle, &writeHandle, &attributes, 4096))
        {
            return -1;
        }

        fds[0] = ::_open_osfhandle(reinterpret_cast<intptr_t>(readHandle), _O_BINARY);
        if (fds[0] < 0)
        {
            (void)::CloseHandle(readHandle);
            (void)::CloseHandle(writeHandle);
            return -1;
        }

        fds[1] = ::_open_osfhandle(reinterpret_cast<intptr_t>(writeHandle), _O_BINARY);
        if (fds[1] < 0)
        {
            (void)::_close(fds[0]);
            (void)::CloseHandle(writeHandle);
            return -1;
        }

        return 0;
    }

    intptr_t GetOsHandle(int fd)
    {
        const auto previousHandler = ::_set_thread_local_invalid_parameter_handler(IgnoreInvalidParameter);
        const intptr_t handle = ::_get_osfhandle(fd);
        (void)::_set_thread_local_invalid_parameter_handler(previousHandler);
        return handle;
    }

    void WritePlatform(Stream /*stream*/, const std::string& line)
    {
        std::string output{line};
        output.push_back('\n');
        ::OutputDebugStringA(output.c_str());
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
        // OutputDebugStringA (~4 KiB practical). Leave headroom under 4096.
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

            // Consume complete lines via a start index so we only memmove once
            // per read batch instead of on every newline.
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
        // Prefer the secure CRT form; UWP treats the deprecated _open as an error.
        int nullFd{-1};
        if (::_sopen_s(&nullFd, "NUL", _O_WRONLY | _O_BINARY, _SH_DENYNO, 0) != 0)
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

    bool RestoreStandardHandle(const Channel& channel)
    {
        HANDLE handle = channel.OriginalHandle;
        if (channel.OriginalHandleUsesTarget)
        {
            const intptr_t restoredHandle = GetOsHandle(channel.Target);
            if (restoredHandle == -1)
            {
                return false;
            }
            handle = reinterpret_cast<HANDLE>(restoredHandle);
        }
        return ::SetStdHandle(channel.StandardHandle, handle) != FALSE;
    }

    bool StartChannel(Channel& channel, int target, Stream stream)
    {
        channel.Target = target;
        channel.StandardHandle = stream == Stream::Error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
        channel.OriginalHandle = ::GetStdHandle(channel.StandardHandle);
        const intptr_t targetHandle = GetOsHandle(target);
        channel.OriginalHandleUsesTarget =
            targetHandle != -1 &&
            channel.OriginalHandle != nullptr &&
            channel.OriginalHandle != INVALID_HANDLE_VALUE &&
            channel.OriginalHandle == reinterpret_cast<HANDLE>(targetHandle);

        errno = 0;
        const auto previousHandler = ::_set_thread_local_invalid_parameter_handler(IgnoreInvalidParameter);
        channel.Original = Duplicate(target);
        (void)::_set_thread_local_invalid_parameter_handler(previousHandler);

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

        const intptr_t pipeHandle = GetOsHandle(target);
        if (pipeHandle == -1 || !::SetStdHandle(channel.StandardHandle, reinterpret_cast<HANDLE>(pipeHandle)))
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
            (void)RestoreStandardHandle(channel);
            (void)Close(pipeFds[0]);
            channel = {};
            return false;
        }

        int readerOriginal{-1};
        if (channel.Original >= 0)
        {
            readerOriginal = Duplicate(channel.Original);
            if (readerOriginal < 0)
            {
                (void)DuplicateTo(channel.Original, target);
                (void)Close(channel.Original);
                (void)RestoreStandardHandle(channel);
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
            (void)RestoreStandardHandle(channel);
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

        restored = RestoreStandardHandle(channel) && restored;

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
                // Detach so Stop can return, but keep the future so Start()
                // can refuse a restart until this drain actually finishes.
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