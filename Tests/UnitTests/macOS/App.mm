#include "../Shared/Shared.h"
#include "Babylon/DebugTrace.h"
#import <Foundation/Foundation.h>

int main(int argc, char** argv)
{
    Babylon::DebugTrace::EnableDebugTrace(true);
    Babylon::DebugTrace::SetTraceOutput([](const char* trace) { NSLog(@"%@", trace); });

    return RunTests(argc, argv);
}
