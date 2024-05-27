#include "../Shared/Shared.h"
#include <Windows.h>
#include "Babylon/DebugTrace.h"

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    Babylon::DebugTrace::EnableDebugTrace(true);
    Babylon::DebugTrace::SetTraceOutput([](const char* trace) { OutputDebugStringA(trace); });

    DEBUG_RUN([]() {
        DEBUG_TRACE("This will stop the debugger");
        DebugBreak();
        });
    return RunTests();
}
