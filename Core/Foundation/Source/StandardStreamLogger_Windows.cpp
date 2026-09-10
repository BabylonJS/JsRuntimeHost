#include "StandardStreamLoggerPlatform.h"

#include <cerrno>
#include <cstdint>
#include <string>

#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>

namespace
{
    constexpr intptr_t NO_CONSOLE_FILENO{-2};
    // Some Windows SDKs hide HANDLE_FLAG_INHERIT from the app partition.
    constexpr DWORD HANDLE_INHERIT_FLAG{0x00000001};

    void IgnoreInvalidParameter(
        const wchar_t*,
        const wchar_t*,
        const wchar_t*,
        unsigned int,
        uintptr_t)
    {
    }

    // Win32/UWP need to keep GetStdHandle/SetStdHandle in sync with CRT fds.
    struct ChannelPlatformState
    {
        DWORD StandardHandle{};
        HANDLE OriginalHandle{INVALID_HANDLE_VALUE};
        DWORD OriginalDescriptorHandleFlags{};
        bool OriginalDescriptorOpen{};
        bool OriginalHandleUsesTarget{};
    };

    void SetErrnoFromWin32Error(DWORD error)
    {
        _doserrno = error;
        switch (error)
        {
        case ERROR_INVALID_HANDLE:
            errno = EBADF;
            break;
        case ERROR_TOO_MANY_OPEN_FILES:
            errno = EMFILE;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            errno = ENOMEM;
            break;
        case ERROR_ACCESS_DENIED:
            errno = EACCES;
            break;
        case ERROR_INVALID_PARAMETER:
            errno = EINVAL;
            break;
        case ERROR_BROKEN_PIPE:
            errno = EPIPE;
            break;
        default:
            errno = EIO;
            break;
        }
    }

    intptr_t GetOsHandle(int fd)
    {
        const auto previousHandler = ::_set_thread_local_invalid_parameter_handler(IgnoreInvalidParameter);
        const intptr_t handle = ::_get_osfhandle(fd);
        (void)::_set_thread_local_invalid_parameter_handler(previousHandler);
        return handle;
    }

    bool SetDescriptorInheritance(int fd, bool inherit)
    {
        const intptr_t handle = GetOsHandle(fd);
        if (handle == -1)
        {
            return false;
        }
        if (!::SetHandleInformation(
                reinterpret_cast<HANDLE>(handle),
                HANDLE_INHERIT_FLAG,
                inherit ? HANDLE_INHERIT_FLAG : 0))
        {
            SetErrnoFromWin32Error(::GetLastError());
            return false;
        }
        return true;
    }

    int OsDuplicate(int fd)
    {
        const intptr_t sourceHandle = GetOsHandle(fd);
        if (sourceHandle == -1)
        {
            return -1;
        }
        if (sourceHandle == NO_CONSOLE_FILENO)
        {
            errno = EBADF;
            _doserrno = 0;
            return -1;
        }

        // _dup preserves the source descriptor's complete CRT state (including
        // text mode), unlike rebuilding it with _open_osfhandle.
        const auto previousHandler = ::_set_thread_local_invalid_parameter_handler(IgnoreInvalidParameter);
        const int duplicated = ::_dup(fd);
        (void)::_set_thread_local_invalid_parameter_handler(previousHandler);

        if (duplicated < 0)
        {
            return -1;
        }
        if (!SetDescriptorInheritance(duplicated, false))
        {
            const int error = errno;
            const unsigned long dosError = _doserrno;
            (void)::_close(duplicated);
            errno = error;
            _doserrno = dosError;
            return -1;
        }
        return duplicated;
    }

    int OsDuplicateTo(int source, int target)
    {
        return ::_dup2(source, target);
    }

    int OsClose(int fd)
    {
        return ::_close(fd);
    }

    int64_t OsRead(int fd, void* data, size_t size)
    {
        return ::_read(fd, data, static_cast<unsigned int>(size));
    }

    int64_t OsWrite(int fd, const void* data, size_t size)
    {
        return ::_write(fd, data, static_cast<unsigned int>(size));
    }

    int OsCreatePipe(int fds[2])
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
            SetErrnoFromWin32Error(::GetLastError());
            return -1;
        }

        fds[0] = ::_open_osfhandle(reinterpret_cast<intptr_t>(readHandle), _O_BINARY | _O_NOINHERIT);
        if (fds[0] < 0)
        {
            (void)::CloseHandle(readHandle);
            (void)::CloseHandle(writeHandle);
            return -1;
        }

        fds[1] = ::_open_osfhandle(reinterpret_cast<intptr_t>(writeHandle), _O_BINARY | _O_NOINHERIT);
        if (fds[1] < 0)
        {
            (void)::_close(fds[0]);
            (void)::CloseHandle(writeHandle);
            return -1;
        }

        return 0;
    }

    bool OsOccupyTarget(int target)
    {
        // Prefer the secure CRT form; UWP treats the deprecated _open as an error.
        int nullFd{-1};
        const errno_t openError =
            ::_sopen_s(&nullFd, "NUL", _O_WRONLY | _O_BINARY | _O_NOINHERIT, _SH_DENYNO, 0);
        if (openError != 0)
        {
            errno = openError;
            return false;
        }
        if (nullFd == target)
        {
            return true;
        }

        const bool targetDuplicated = OsDuplicateTo(nullFd, target) == 0;
        const bool duplicated =
            targetDuplicated &&
            SetDescriptorInheritance(target, false);
        const int error = errno;
        const unsigned long dosError = _doserrno;
        (void)OsClose(nullFd);
        if (!duplicated)
        {
            if (targetDuplicated)
            {
                (void)OsClose(target);
            }
            errno = error;
            _doserrno = dosError;
        }
        return duplicated;
    }

    size_t OsMaxPlatformLineSize(bool /*isError*/)
    {
        return 3800;
    }

    void OsWritePlatform(bool /*isError*/, const std::string& line)
    {
        std::string output{line};
        output.push_back('\n');
        ::OutputDebugStringA(output.c_str());
    }

    bool OsOnStartChannel(ChannelPlatformState& state, int target, bool isError)
    {
        state.StandardHandle = isError ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
        state.OriginalHandle = ::GetStdHandle(state.StandardHandle);
        const intptr_t targetHandle = GetOsHandle(target);
        if (targetHandle == -1)
        {
            return errno == EBADF;
        }
        if (targetHandle == NO_CONSOLE_FILENO)
        {
            return true;
        }

        if (!::GetHandleInformation(
                reinterpret_cast<HANDLE>(targetHandle),
                &state.OriginalDescriptorHandleFlags))
        {
            SetErrnoFromWin32Error(::GetLastError());
            return false;
        }

        state.OriginalDescriptorOpen = true;
        state.OriginalHandleUsesTarget =
            state.OriginalHandle != nullptr &&
            state.OriginalHandle != INVALID_HANDLE_VALUE &&
            state.OriginalHandle == reinterpret_cast<HANDLE>(targetHandle);
        return true;
    }

    bool OsOnRedirected(ChannelPlatformState& state, int target)
    {
        const intptr_t pipeHandle = GetOsHandle(target);
        if (pipeHandle == -1)
        {
            return false;
        }

        const bool inherit =
            state.OriginalDescriptorOpen &&
            (state.OriginalDescriptorHandleFlags & HANDLE_INHERIT_FLAG) != 0;
        if (!SetDescriptorInheritance(target, inherit))
        {
            return false;
        }
        if (!::SetStdHandle(state.StandardHandle, reinterpret_cast<HANDLE>(pipeHandle)))
        {
            SetErrnoFromWin32Error(::GetLastError());
            return false;
        }
        return true;
    }

    bool OsOnRestore(ChannelPlatformState& state, int target)
    {
        HANDLE handle = state.OriginalHandle;
        bool restored{true};
        if (state.OriginalDescriptorOpen)
        {
            const intptr_t restoredHandle = GetOsHandle(target);
            if (restoredHandle == -1)
            {
                return false;
            }

            const bool inherit =
                (state.OriginalDescriptorHandleFlags & HANDLE_INHERIT_FLAG) != 0;
            restored = SetDescriptorInheritance(target, inherit);
            if (state.OriginalHandleUsesTarget)
            {
                handle = reinterpret_cast<HANDLE>(restoredHandle);
            }
        }
        if (!::SetStdHandle(state.StandardHandle, handle))
        {
            SetErrnoFromWin32Error(::GetLastError());
            return false;
        }
        return restored;
    }
}

#include "StandardStreamLogger_Shared.inl"