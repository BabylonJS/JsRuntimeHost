#include "../Shared/Shared.h"
#include "Babylon/DebugTrace.h"
#include <cstdio>

int main(int argc, char** argv)
{
    Babylon::DebugTrace::EnableDebugTrace(true);
    Babylon::DebugTrace::SetTraceOutput([](const char* trace) { printf("%s\n", trace); fflush(stdout); });

    return RunTests(argc, argv);
}
