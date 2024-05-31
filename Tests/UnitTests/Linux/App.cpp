#include "../Shared/Shared.h"
#include "Babylon/DebugTrace.h"

int main()
{
    Babylon::DebugTrace::EnableDebugTrace(true);
    Babylon::DebugTrace::SetTraceOutput([](const char* trace) { printf("%s\n", trace); });

    return RunTests();
}
