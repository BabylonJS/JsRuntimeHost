#include "StandardStreamLogger.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#elif defined(__ANDROID__)
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <os/log.h>
#include <unistd.h>
#endif

namespace
{
#if defined(_WIN32) || defined(__ANDROID__) || defined(__APPLE__)
    enum class Stream
    {
        Output,
        Error,
    };

    struct Channel
    {
        int Target{-1};
        int Original{-1};
#if defined(_WIN32)
        DWORD StandardHandle{};
        HANDLE OriginalHandle{INVALID_HANDLE_VALUE};
        bool OriginalHandleUsesTarget{};
#endif
        std::future<void> Completion{};
        std::thread Reader{};
    };

    // Drain futures retained after Stop() times out and detaches the reader.
    // Start() reaps these before installing a new redirection.
    std::vector<std::future<void>> g_outstandingDrains{};

#if defined(_WIN32)
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
#else
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
#endif

    void WritePlatform(Stream stream, const std::string& line)
    {
#if defined(_WIN32)
        (void)stream;
        std::string output{line};
        output.push_back('\n');
        ::OutputDebugStringA(output.c_str());
#elif defined(__ANDROID__)
        const int priority = stream == Stream::Error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO;
        __android_log_write(priority, "JsRuntimeHost", line.c_str());
#elif defined(__APPLE__)
        const os_log_type_t type = stream == Stream::Error ? OS_LOG_TYPE_ERROR : OS_LOG_TYPE_DEFAULT;
        os_log_with_type(OS_LOG_DEFAULT, type, "%{public}s", line.c_str());
#endif
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
            // OutputDebugStringA (~4 KiB practical), Android logcat (~4 KiB),
            // and Apple os_log payload limits. Leave headroom under 4096.
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
#if defined(_WIN32)
        // Prefer the secure CRT form; UWP treats the deprecated _open as an error.
        int nullFd{-1};
        if (::_sopen_s(&nullFd, "NUL", _O_WRONLY | _O_BINARY, _SH_DENYNO, 0) != 0)
        {
            return false;
        }
#else
        const int nullFd = ::open("/dev/null", O_WRONLY);
        if (nullFd < 0)
        {
            return false;
        }
#endif
        if (nullFd == target)
        {
            return true;
        }

        const bool duplicated = DuplicateTo(nullFd, target) == 0;
        (void)Close(nullFd);
        return duplicated;
    }

#if defined(_WIN32)
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
#endif

    bool StartChannel(Channel& channel, int target, Stream stream)
    {
        channel.Target = target;
#if defined(_WIN32)
        channel.StandardHandle = stream == Stream::Error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
        channel.OriginalHandle = ::GetStdHandle(channel.StandardHandle);
        const intptr_t targetHandle = GetOsHandle(target);
        channel.OriginalHandleUsesTarget =
            targetHandle != -1 &&
            channel.OriginalHandle != nullptr &&
            channel.OriginalHandle != INVALID_HANDLE_VALUE &&
            channel.OriginalHandle == reinterpret_cast<HANDLE>(targetHandle);
#endif
        errno = 0;
#if defined(_WIN32)
        const auto previousHandler = ::_set_thread_local_invalid_parameter_handler(IgnoreInvalidParameter);
#endif
        channel.Original = Duplicate(target);
#if defined(_WIN32)
        (void)::_set_thread_local_invalid_parameter_handler(previousHandler);
#endif
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

#if defined(_WIN32)
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
#endif

        int readerOriginal{-1};
        if (channel.Original >= 0)
        {
            readerOriginal = Duplicate(channel.Original);
            if (readerOriginal < 0)
            {
                (void)DuplicateTo(channel.Original, target);
                (void)Close(channel.Original);
#if defined(_WIN32)
                (void)RestoreStandardHandle(channel);
#endif
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
#if defined(_WIN32)
            (void)RestoreStandardHandle(channel);
#endif
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

#if defined(_WIN32)
        restored = RestoreStandardHandle(channel) && restored;
#endif

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
                        // Otherwise a second Start() would spin up concurrent drains
                        // and duplicate/out-of-order platform logging.
                        g_outstandingDrains.push_back(std::move(channel.Completion));
                        channel.Reader.detach();
                        restored = false;
                    }
                }
                channel = {};
                return restored;
            }

            // Drop completed drains; optionally wait up to `timeout` for the rest.
            // Returns true only when no outstanding drains remain.
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
        #endif

        std::mutex g_mutex{};
            bool g_started{};
            bool g_exitHandlerRegistered{};
        #if defined(_WIN32) || defined(__ANDROID__) || defined(__APPLE__)
            Channel g_stdout{};
            Channel g_stderr{};
        #endif
        }

namespace Babylon::StandardStreamLogger
{
    bool Start()
    {
        std::lock_guard<std::mutex> lock{g_mutex};
        if (g_started)
        {
            return true;
        }

#if defined(_WIN32) || defined(__ANDROID__) || defined(__APPLE__)
            // A prior Stop() may have detached drain threads after timeout. Do not
            // redirect again until those finish; otherwise concurrent drains can
            // duplicate platform logs against the restored (or newly teed) streams.
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
    #endif

        if (!g_exitHandlerRegistered)
        {
            if (std::atexit([] {
                    (void)Babylon::StandardStreamLogger::Stop();
                }) != 0)
            {
#if defined(_WIN32) || defined(__ANDROID__) || defined(__APPLE__)
                (void)StopChannel(g_stdout);
                (void)StopChannel(g_stderr);
#endif
                return false;
            }
            g_exitHandlerRegistered = true;
        }

        g_started = true;
        return true;
    }

    bool Stop()
    {
        std::lock_guard<std::mutex> lock{g_mutex};
        if (!g_started)
        {
            return true;
        }

        bool stopped{true};
#if defined(_WIN32) || defined(__ANDROID__) || defined(__APPLE__)
        std::cout.flush();
        std::cerr.flush();
        std::fflush(stdout);
        std::fflush(stderr);
        stopped = StopChannel(g_stdout);
        stopped = StopChannel(g_stderr) && stopped;
#endif
        g_started = false;
        return stopped;
    }

    bool IsStarted()
    {
        std::lock_guard<std::mutex> lock{g_mutex};
        return g_started;
    }
}
