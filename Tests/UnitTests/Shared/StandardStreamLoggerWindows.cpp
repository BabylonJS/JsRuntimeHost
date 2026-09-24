#include <Babylon/StandardStreamLogger.h>
#include <gtest/gtest.h>

#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
namespace
{
    constexpr DWORD InheritHandleFlag{0x00000001};
    constexpr size_t DescriptorReservationCount{32};
    constexpr char SpawnProbeSwitch[]{"--standard-stream-logger-spawn-probe"};

    void IgnoreInvalidParameter(
        const wchar_t*,
        const wchar_t*,
        const wchar_t*,
        unsigned int,
        uintptr_t)
    {
    }

    intptr_t GetOsHandle(int fd)
    {
        const auto previousHandler = ::_set_thread_local_invalid_parameter_handler(IgnoreInvalidParameter);
        const intptr_t handle = ::_get_osfhandle(fd);
        (void)::_set_thread_local_invalid_parameter_handler(previousHandler);
        return handle;
    }

    int DescriptorInheritance(int fd)
    {
        DWORD flags{};
        const intptr_t handle = GetOsHandle(fd);
        return handle != -1 && ::GetHandleInformation(reinterpret_cast<HANDLE>(handle), &flags)
                   ? (flags & InheritHandleFlag) != 0
                   : -1;
    }

    bool SetDescriptorInheritance(int fd, bool inherit)
    {
        const intptr_t handle = GetOsHandle(fd);
        return handle != -1 && ::SetHandleInformation(
                                   reinterpret_cast<HANDLE>(handle), InheritHandleFlag, inherit ? InheritHandleFlag : 0);
    }

    bool BindNonInheritableDescriptor(int source, int target, int mode)
    {
        const intptr_t sourceHandle = GetOsHandle(source);
        if (sourceHandle == -1)
        {
            return false;
        }
        HANDLE handle{INVALID_HANDLE_VALUE};
        if (!::DuplicateHandle(
                ::GetCurrentProcess(), reinterpret_cast<HANDLE>(sourceHandle),
                ::GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS))
        {
            return false;
        }
        if (::_close(target) != 0)
        {
            (void)::CloseHandle(handle);
            return false;
        }
        const int reopened =
            ::_open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_BINARY | _O_NOINHERIT);
        if (reopened != target)
        {
            if (reopened < 0)
            {
                (void)::CloseHandle(handle);
            }
            else
            {
                (void)::_close(reopened);
            }
            return false;
        }
        return ::_setmode(target, mode) >= 0;
    }

    FILE* FileForDescriptor(int fd)
    {
        return fd == 1 ? stdout : stderr;
    }

    DWORD StandardHandleForDescriptor(int fd)
    {
        return fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE;
    }

    class StreamCapture
    {
    public:
        explicit StreamCapture(int target, bool noinherit = false)
            : m_target{target}
            , m_standardHandleId{StandardHandleForDescriptor(target)}
        {
            std::fflush(FileForDescriptor(m_target));
            m_originalInheritance = DescriptorInheritance(m_target);
            m_original = ::_dup(m_target);
            m_originalStdHandle = ::GetStdHandle(m_standardHandleId);
            const intptr_t originalHandle = GetOsHandle(m_target);
            m_originalStdHandleUsesTarget =
                originalHandle != -1 &&
                m_originalStdHandle != nullptr &&
                m_originalStdHandle != INVALID_HANDLE_VALUE &&
                m_originalStdHandle == reinterpret_cast<HANDLE>(originalHandle);

            m_file = std::tmpfile();
            if (m_original < 0 || m_file == nullptr)
            {
                return;
            }

            m_fileDescriptor = ::_fileno(m_file);
            if (m_fileDescriptor < 0 || ::_setmode(m_fileDescriptor, _O_BINARY) < 0 ||
                (noinherit
                        ? !BindNonInheritableDescriptor(m_fileDescriptor, m_target, _O_BINARY)
                        : ::_dup2(m_fileDescriptor, m_target) != 0))
            {
                return;
            }

            if ((m_originalInheritance >= 0 &&
                    !SetDescriptorInheritance(m_target, !noinherit && m_originalInheritance != 0)) ||
                !::SetStdHandle(
                    m_standardHandleId,
                    reinterpret_cast<HANDLE>(GetOsHandle(m_target))))
            {
                return;
            }

            m_valid = true;
        }

        ~StreamCapture()
        {
            RestoreTarget();
            CloseFile();
        }

        bool Valid() const
        {
            return m_valid;
        }

        bool ReadAndRestore(std::string& output)
        {
            std::fflush(FileForDescriptor(m_target));
            const bool restored = RestoreTarget();

            bool read{m_fileDescriptor >= 0 && ::_setmode(m_fileDescriptor, _O_BINARY) >= 0};
            if (read)
            {
                read = ::_lseeki64(m_fileDescriptor, 0, SEEK_SET) == 0;
            }

            std::array<char, 256> buffer{};
            while (read)
            {
                const int count = ::_read(m_fileDescriptor, buffer.data(), static_cast<unsigned int>(buffer.size()));
                if (count < 0)
                {
                    read = false;
                    break;
                }
                if (count == 0)
                {
                    break;
                }
                output.append(buffer.data(), static_cast<size_t>(count));
            }

            CloseFile();
            return restored && read;
        }

    private:
        bool RestoreTarget()
        {
            bool restored{true};
            if (m_original >= 0)
            {
                std::fflush(FileForDescriptor(m_target));
                bool descriptorRestored{};
                if (m_originalInheritance == 0)
                {
                    const int originalMode = ::_setmode(m_original, _O_BINARY);
                    if (originalMode >= 0)
                    {
                        descriptorRestored =
                            BindNonInheritableDescriptor(m_original, m_target, originalMode);
                    }
                }
                else
                {
                    descriptorRestored = ::_dup2(m_original, m_target) == 0;
                }
                restored = descriptorRestored;
                if (descriptorRestored && m_originalInheritance >= 0)
                {
                    restored =
                        SetDescriptorInheritance(m_target, m_originalInheritance != 0) &&
                        restored;
                }

                (void)::_close(m_original);
                m_original = -1;

                if (descriptorRestored)
                {
                    HANDLE handle = m_originalStdHandle;
                    if (m_originalStdHandleUsesTarget)
                    {
                        const intptr_t restoredHandle = GetOsHandle(m_target);
                        handle = restoredHandle == -1
                                     ? INVALID_HANDLE_VALUE
                                     : reinterpret_cast<HANDLE>(restoredHandle);
                    }
                    restored =
                        ::SetStdHandle(m_standardHandleId, handle) != FALSE &&
                        restored;
                }
            }
            m_valid = false;
            return restored;
        }

        void CloseFile()
        {
            if (m_file != nullptr)
            {
                std::fclose(m_file);
                m_file = nullptr;
                m_fileDescriptor = -1;
            }
        }

        int m_target;
        DWORD m_standardHandleId;
        FILE* m_file{};
        int m_fileDescriptor{-1};
        int m_original{-1};
        int m_originalInheritance{-1};
        HANDLE m_originalStdHandle{INVALID_HANDLE_VALUE};
        bool m_originalStdHandleUsesTarget{};
        bool m_valid{};
    };

    struct ModeCase
    {
        const char* Name;
        int Mode;
        int ObservableMode;
        bool UnicodeInput;
    };

    constexpr std::array<ModeCase, 5> ModeCases{{
        {"binary", _O_BINARY, _O_BINARY, false},
        {"text", _O_TEXT, _O_TEXT, false},
        {"utf8", _O_U8TEXT, _O_U8TEXT, true},
        {"utf16", _O_U16TEXT, _O_WTEXT, true},
        {"wtext", _O_WTEXT, _O_WTEXT, true},
    }};

    bool WriteStage(int target, const ModeCase& mode, wchar_t stage)
    {
        if (!mode.UnicodeInput)
        {
            const std::array<char, 2> input{{static_cast<char>(stage), '\n'}};
            return ::_write(target, input.data(), static_cast<unsigned int>(input.size())) ==
                   static_cast<int>(input.size());
        }

        const std::array<wchar_t, 3> input{{stage, L'\x2603', L'\n'}};
        const auto size = static_cast<unsigned int>(input.size() * sizeof(wchar_t));
        return ::_write(target, input.data(), size) == static_cast<int>(size);
    }

    void AppendUtf16(std::string& output, wchar_t value)
    {
        output.append(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    void AppendExpectedStage(std::string& output, const ModeCase& mode, wchar_t stage)
    {
        if (mode.Mode == _O_BINARY)
        {
            output.push_back(static_cast<char>(stage));
            output.push_back('\n');
            return;
        }
        if (mode.Mode == _O_TEXT)
        {
            output.push_back(static_cast<char>(stage));
            output.append("\r\n");
            return;
        }
        if (mode.Mode == _O_U8TEXT)
        {
            output.push_back(static_cast<char>(stage));
            output.append("\xE2\x98\x83\r\n");
            return;
        }

        AppendUtf16(output, stage);
        AppendUtf16(output, L'\x2603');
        AppendUtf16(output, L'\r');
        AppendUtf16(output, L'\n');
    }

    struct ModeResult
    {
        bool WasAlreadyStarted{};
        bool CaptureValid{};
        bool ModeSet{};
        bool BeforeWritten{};
        bool Started{};
        int ActiveMode{-1};
        bool ActiveModeRestored{};
        bool DuringWritten{};
        bool Stopped{};
        int RestoredMode{-1};
        bool RestoredModeRestored{};
        bool AfterWritten{};
        bool CaptureRead{};
        std::string Captured{};
    };

    ModeResult RunModeCase(int target, const ModeCase& mode)
    {
        ModeResult result{};
        result.WasAlreadyStarted = Babylon::StandardStreamLogger::IsStarted();
        if (result.WasAlreadyStarted)
        {
            return result;
        }

        StreamCapture capture{target};
        result.CaptureValid = capture.Valid();
        if (!result.CaptureValid)
        {
            return result;
        }

        result.ModeSet = ::_setmode(target, mode.Mode) >= 0;
        if (result.ModeSet)
        {
            result.BeforeWritten = WriteStage(target, mode, L'B');
        }

        result.Started = Babylon::StandardStreamLogger::Start();
        if (result.Started)
        {
            result.ActiveMode = ::_setmode(target, mode.Mode);
            if (result.ActiveMode >= 0)
            {
                result.ActiveModeRestored = ::_setmode(target, result.ActiveMode) >= 0;
            }
            result.DuringWritten = WriteStage(target, mode, L'D');
        }

        result.Stopped = Babylon::StandardStreamLogger::Stop();
        if (result.Started && result.Stopped)
        {
            result.RestoredMode = ::_setmode(target, mode.Mode);
            if (result.RestoredMode >= 0)
            {
                result.RestoredModeRestored = ::_setmode(target, result.RestoredMode) >= 0;
            }
            result.AfterWritten = WriteStage(target, mode, L'A');
        }

        result.CaptureRead = capture.ReadAndRestore(result.Captured);
        return result;
    }

    std::vector<int> AllocateDescriptorReservations(size_t count)
    {
        std::vector<int> result{};
        result.reserve(count);
        for (size_t index = 0; index < count; ++index)
        {
            const int fd = ::_dup(1);
            if (fd < 0)
            {
                break;
            }
            result.push_back(fd);
        }
        return result;
    }

    void CloseDescriptors(const std::vector<int>& descriptors)
    {
        for (const int fd : descriptors)
        {
            (void)::_close(fd);
        }
    }

    std::vector<int> FindOccupiedReservations(
        const std::vector<int>& reservations,
        const std::vector<int>& availableAfterStart)
    {
        std::vector<int> result{};
        for (const int fd : reservations)
        {
            if (std::find(availableAfterStart.begin(), availableAfterStart.end(), fd) ==
                availableAfterStart.end())
            {
                result.push_back(fd);
            }
        }
        return result;
    }

    struct SpawnResult
    {
        bool Started{};
        bool ExecutableFound{};
        bool Spawned{};
        bool Waited{};
        DWORD ExitCode{static_cast<DWORD>(-1)};
        bool Stopped{};
        bool AfterStopSpawned{};
        bool AfterStopWaited{};
        DWORD AfterStopExitCode{static_cast<DWORD>(-1)};
        std::vector<int> PrivateDescriptors{};
    };

    struct SpawnOutcome
    {
        bool Spawned{};
        bool Waited{};
        DWORD ExitCode{static_cast<DWORD>(-1)};
    };

    SpawnOutcome ProbeSpawn(const std::wstring& executable, const std::vector<int>& descriptors)
    {
        SpawnOutcome result{};
        std::vector<std::wstring> argumentStorage{};
        argumentStorage.reserve(descriptors.size() + 2);
        argumentStorage.push_back(executable);
        argumentStorage.emplace_back(L"--standard-stream-logger-spawn-probe");
        for (const int fd : descriptors)
        {
            argumentStorage.push_back(std::to_wstring(fd));
        }

        std::vector<const wchar_t*> arguments{};
        arguments.reserve(argumentStorage.size() + 1);
        for (const auto& argument : argumentStorage)
        {
            arguments.push_back(argument.c_str());
        }
        arguments.push_back(nullptr);

        const intptr_t child = ::_wspawnv(
            _P_NOWAIT, argumentStorage.front().c_str(), arguments.data());
        result.Spawned = child != -1;
        if (result.Spawned)
        {
            const HANDLE process = reinterpret_cast<HANDLE>(child);
            const DWORD wait = ::WaitForSingleObject(process, 10000);
            result.Waited = wait == WAIT_OBJECT_0;
            if (!result.Waited && wait == WAIT_TIMEOUT)
            {
                (void)::TerminateProcess(process, 0xFE);
                (void)::WaitForSingleObject(process, 10000);
            }
            if (result.Waited)
            {
                (void)::GetExitCodeProcess(process, &result.ExitCode);
            }
            (void)::CloseHandle(process);
        }
        return result;
    }

    SpawnResult RunSpawnCase(bool checkStandardDescriptors = false)
    {
        SpawnResult result{};
        if (Babylon::StandardStreamLogger::IsStarted())
        {
            return result;
        }

        const std::vector<int> reservations =
            AllocateDescriptorReservations(DescriptorReservationCount);
        CloseDescriptors(reservations);

        std::vector<wchar_t> executableBuffer(32768);
        DWORD executableLength{};
        result.Started = Babylon::StandardStreamLogger::Start();
        if (result.Started)
        {
            const std::vector<int> availableAfterStart =
                AllocateDescriptorReservations(reservations.size());
            result.PrivateDescriptors =
                FindOccupiedReservations(reservations, availableAfterStart);
            CloseDescriptors(availableAfterStart);

            executableLength = ::GetModuleFileNameW(
                nullptr, executableBuffer.data(), static_cast<DWORD>(executableBuffer.size()));
            result.ExecutableFound =
                executableLength != 0 && executableLength < executableBuffer.size();

            if (result.ExecutableFound && !result.PrivateDescriptors.empty())
            {
                std::vector<int> descriptors = result.PrivateDescriptors;
                if (checkStandardDescriptors)
                {
                    descriptors.push_back(1);
                    descriptors.push_back(2);
                }
                const SpawnOutcome outcome = ProbeSpawn(
                    std::wstring{executableBuffer.data(), executableLength}, descriptors);
                result.Spawned = outcome.Spawned;
                result.Waited = outcome.Waited;
                result.ExitCode = outcome.ExitCode;
            }
        }

        result.Stopped = Babylon::StandardStreamLogger::Stop();
        if (checkStandardDescriptors && result.Stopped && result.ExecutableFound)
        {
            const SpawnOutcome outcome = ProbeSpawn(
                std::wstring{executableBuffer.data(), executableLength}, {1, 2});
            result.AfterStopSpawned = outcome.Spawned;
            result.AfterStopWaited = outcome.Waited;
            result.AfterStopExitCode = outcome.ExitCode;
        }
        return result;
    }
}

int RunStandardStreamLoggerSpawnProbe(int argc, char** argv)
{
    if (argc < 3 || std::strcmp(argv[1], SpawnProbeSwitch) != 0)
    {
        return 2;
    }

    STARTUPINFOA startupInfo{};
    ::GetStartupInfoA(&startupInfo);
    if (startupInfo.lpReserved2 == nullptr || startupInfo.cbReserved2 < sizeof(int))
    {
        return 3;
    }

    int handleCount{};
    std::memcpy(&handleCount, startupInfo.lpReserved2, sizeof(handleCount));
    if (handleCount < 0)
    {
        return 4;
    }

    const size_t requiredSize =
        sizeof(handleCount) +
        static_cast<size_t>(handleCount) * (sizeof(unsigned char) + sizeof(intptr_t));
    if (requiredSize > startupInfo.cbReserved2)
    {
        return 5;
    }

    const auto* firstFlag =
        reinterpret_cast<const unsigned char*>(startupInfo.lpReserved2) + sizeof(handleCount);
    const auto* firstHandle = firstFlag + handleCount;
    for (int argument = 2; argument < argc; ++argument)
    {
        char* end{};
        errno = 0;
        const long parsed = std::strtol(argv[argument], &end, 10);
        if (errno != 0 || end == argv[argument] || *end != '\0' || parsed < 0 || parsed > INT_MAX)
        {
            return 6;
        }

        const int fd = static_cast<int>(parsed);
        if (fd >= handleCount)
        {
            continue;
        }

        intptr_t inheritedHandle{};
        std::memcpy(
            &inheritedHandle,
            firstHandle + static_cast<size_t>(fd) * sizeof(inheritedHandle),
            sizeof(inheritedHandle));
        if (firstFlag[fd] != 0 ||
            inheritedHandle != reinterpret_cast<intptr_t>(INVALID_HANDLE_VALUE))
        {
            return 1;
        }
    }

    return 0;
}

TEST(StandardStreamLoggerWindows, PreservesTargetModesAndOriginalBytes)
{
    for (const int target : {1, 2})
    {
        for (const ModeCase& mode : ModeCases)
        {
            const ModeResult result = RunModeCase(target, mode);
            std::string expected{};
            AppendExpectedStage(expected, mode, L'B');
            AppendExpectedStage(expected, mode, L'D');
            AppendExpectedStage(expected, mode, L'A');

            SCOPED_TRACE(
                std::string{target == 1 ? "stdout " : "stderr "} + mode.Name);
            EXPECT_FALSE(result.WasAlreadyStarted);
            EXPECT_TRUE(result.CaptureValid);
            EXPECT_TRUE(result.ModeSet);
            EXPECT_TRUE(result.BeforeWritten);
            EXPECT_TRUE(result.Started);
            EXPECT_EQ(result.ActiveMode, mode.ObservableMode);
            EXPECT_TRUE(result.ActiveModeRestored);
            EXPECT_TRUE(result.DuringWritten);
            EXPECT_TRUE(result.Stopped);
            EXPECT_EQ(result.RestoredMode, mode.ObservableMode);
            EXPECT_TRUE(result.RestoredModeRestored);
            EXPECT_TRUE(result.AfterWritten);
            EXPECT_TRUE(result.CaptureRead);
            EXPECT_EQ(result.Captured, expected);
        }
    }
}

TEST(StandardStreamLoggerWindows, PrivateDescriptorsAreNotSerializedBySpawn)
{
    const SpawnResult result = RunSpawnCase();

    EXPECT_TRUE(result.Started);
    EXPECT_FALSE(result.PrivateDescriptors.empty());
    EXPECT_TRUE(result.ExecutableFound);
    EXPECT_TRUE(result.Spawned);
    EXPECT_TRUE(result.Waited);
    EXPECT_EQ(result.ExitCode, 0u);
    EXPECT_TRUE(result.Stopped);
}

TEST(StandardStreamLoggerWindows, NonInheritableStandardDescriptorsAreNotSerializedBySpawn)
{
    bool capturesValid{};
    SpawnResult result{};
    {
        StreamCapture output{1, true};
        StreamCapture error{2, true};
        capturesValid = output.Valid() && error.Valid();
        if (capturesValid)
        {
            result = RunSpawnCase(true);
        }
    }
    ASSERT_TRUE(capturesValid);
    EXPECT_TRUE(result.Started);
    EXPECT_FALSE(result.PrivateDescriptors.empty());
    EXPECT_TRUE(result.ExecutableFound);
    EXPECT_TRUE(result.Spawned);
    EXPECT_TRUE(result.Waited);
    EXPECT_EQ(result.ExitCode, 0u);
    EXPECT_TRUE(result.Stopped);
    EXPECT_TRUE(result.AfterStopSpawned);
    EXPECT_TRUE(result.AfterStopWaited);
    EXPECT_EQ(result.AfterStopExitCode, 0u);
}
#endif
