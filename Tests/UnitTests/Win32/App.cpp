#include "../Shared/Shared.h"
#include <Windows.h>
#include "Babylon/DebugTrace.h"

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    Babylon::DebugTrace::SetDebugTrace(true);
    Babylon::DebugTrace::SetTraceOutput([](const char* trace) { OutputDebugStringA(trace); });

    return RunTests();
}
