// POSIX fd primitives shared by Android and Apple. Included inside an anonymous
// namespace that already provides OsWritePlatform.

struct ChannelPlatformState
{
    int OriginalDescriptorFlags{};
    bool OriginalDescriptorOpen{};
};

int OsDuplicate(int fd)
{
    return ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
}

int OsDuplicateTo(int source, int target)
{
    return ::dup2(source, target) < 0 ? -1 : 0;
}

int OsClose(int fd)
{
    return ::close(fd);
}

int64_t OsRead(int fd, void* data, size_t size)
{
    return ::read(fd, data, size);
}

int64_t OsWrite(int fd, const void* data, size_t size)
{
    return ::write(fd, data, size);
}

int OsCreatePipe(int fds[2])
{
#if defined(__linux__)
    return ::pipe2(fds, O_CLOEXEC);
#else
    // Darwin has no pipe2; callers must serialize Start() with fork/exec to
    // avoid inheritance between pipe() and fcntl().
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
#endif
}

bool OsOccupyTarget(int target)
{
    const int nullFd = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (nullFd < 0)
    {
        return false;
    }
    if (nullFd == target)
    {
        return true;
    }

    const bool duplicated = OsDuplicateTo(nullFd, target) == 0;
    (void)OsClose(nullFd);
    return duplicated;
}

bool OsOnStartChannel(ChannelPlatformState& state, int target, bool)
{
    state.OriginalDescriptorFlags = ::fcntl(target, F_GETFD);
    state.OriginalDescriptorOpen = state.OriginalDescriptorFlags >= 0;
    return state.OriginalDescriptorOpen || errno == EBADF;
}

bool OsOnRedirected(ChannelPlatformState& state, int target)
{
    const int flags = state.OriginalDescriptorOpen ? state.OriginalDescriptorFlags : FD_CLOEXEC;
    return ::fcntl(target, F_SETFD, flags) == 0;
}

bool OsOnRestore(ChannelPlatformState& state, int target)
{
    return !state.OriginalDescriptorOpen || ::fcntl(target, F_SETFD, state.OriginalDescriptorFlags) == 0;
}