#if !defined(_WIN32)

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

namespace
{
#include "../../../Core/Foundation/Source/StandardStreamLogger_PosixOps.inl"

    struct Descriptor
    {
        int Value;

        ~Descriptor()
        {
            if (Value >= 0)
            {
                (void)OsClose(Value);
            }
        }
    };
}

TEST(StandardStreamLoggerPosix, PrivateDuplicatesAndPipesAreCloseOnExec)
{
    Descriptor original{::open("/dev/null", O_WRONLY)};
    ASSERT_GE(original.Value, 0);
    Descriptor copy{OsDuplicate(original.Value)};
    ASSERT_GE(copy.Value, 0);
    EXPECT_EQ(::fcntl(copy.Value, F_GETFD), FD_CLOEXEC);

    int pipeFds[2];
    ASSERT_EQ(OsCreatePipe(pipeFds), 0);
    Descriptor read{pipeFds[0]};
    Descriptor write{pipeFds[1]};
    EXPECT_EQ(::fcntl(read.Value, F_GETFD), FD_CLOEXEC);
    EXPECT_EQ(::fcntl(write.Value, F_GETFD), FD_CLOEXEC);
    ASSERT_EQ(OsWrite(write.Value, "x", 1), 1);
    char value{};
    EXPECT_EQ(OsRead(read.Value, &value, 1), 1);
    EXPECT_EQ(value, 'x');
}

TEST(StandardStreamLoggerPosix, PreservesInheritanceOnRedirectAndRestore)
{
    for (const int flags : {0, FD_CLOEXEC})
    {
        Descriptor target{::open("/dev/null", O_WRONLY)};
        ASSERT_GE(target.Value, 0);
        ASSERT_EQ(::fcntl(target.Value, F_SETFD, flags), 0);
        ChannelPlatformState state{};
        ASSERT_TRUE(OsOnStartChannel(state, target.Value, false));
        Descriptor original{OsDuplicate(target.Value)};
        ASSERT_GE(original.Value, 0);

        int pipeFds[2];
        ASSERT_EQ(OsCreatePipe(pipeFds), 0);
        Descriptor read{pipeFds[0]};
        Descriptor write{pipeFds[1]};
        ASSERT_EQ(OsDuplicateTo(write.Value, target.Value), 0);
        ASSERT_TRUE(OsOnRedirected(state, target.Value));
        EXPECT_EQ(::fcntl(target.Value, F_GETFD), flags);
        ASSERT_EQ(OsDuplicateTo(original.Value, target.Value), 0);
        ASSERT_TRUE(OsOnRestore(state, target.Value));
        EXPECT_EQ(::fcntl(target.Value, F_GETFD), flags);
    }
}

TEST(StandardStreamLoggerPosix, SupportsAnInitiallyClosedTarget)
{
    Descriptor target{::open("/dev/null", O_WRONLY)};
    ASSERT_GE(target.Value, 0);
    const int targetFd = target.Value;
    ASSERT_EQ(OsClose(targetFd), 0);
    target.Value = -1;
    ChannelPlatformState state{};
    ASSERT_TRUE(OsOnStartChannel(state, targetFd, false));
    ASSERT_TRUE(OsOccupyTarget(targetFd));
    target.Value = targetFd;
    ASSERT_TRUE(OsOnRedirected(state, target.Value));
    EXPECT_NE(::fcntl(target.Value, F_GETFD) & FD_CLOEXEC, 0);
    ASSERT_EQ(OsClose(target.Value), 0);
    target.Value = -1;
    ASSERT_TRUE(OsOnRestore(state, targetFd));
    EXPECT_EQ(::fcntl(targetFd, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(StandardStreamLoggerPosix, InvalidDuplicateReportsBadDescriptor)
{
    errno = 0;
    EXPECT_EQ(OsDuplicate(-1), -1);
    EXPECT_EQ(errno, EBADF);
}

#endif
