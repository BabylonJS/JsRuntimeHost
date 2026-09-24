#include "../Shared/Shared.h"
#include <Windows.h>
#include <cstring>
#include "Babylon/DebugTrace.h"
#include <gtest/gtest.h>

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
int RunStandardStreamLoggerSpawnProbe(int argc, char** argv);
#endif

int main(int argc, char** argv)
{
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    if (argc >= 2 && std::strcmp(argv[1], "--standard-stream-logger-spawn-probe") == 0)
    {
        return RunStandardStreamLoggerSpawnProbe(argc, argv);
    }
#endif

    SetConsoleOutputCP(CP_UTF8);

    Babylon::DebugTrace::EnableDebugTrace(true);
    Babylon::DebugTrace::SetTraceOutput([](const char* trace) { OutputDebugStringA(trace); });

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
