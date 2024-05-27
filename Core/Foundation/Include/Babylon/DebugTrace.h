#pragma once
#include <functional>

#if JSRUNTIMEHOST_DEBUG_TRACE
#define _STRINGIFY(x) #x
#define _TOSTRING(x) _STRINGIFY(x)
#define _DEBUG_TRACE(format, ...) Babylon::DebugTrace::Trace(__FILE__ "(" _TOSTRING(__LINE__) "): " format "\n", ##__VA_ARGS__); 
#define _DEBUG_RUN(lambda) lambda();

#define DEBUG_TRACE _DEBUG_TRACE
#define DEBUG_RUN _DEBUG_RUN
#else // No debugging trace enabled
#define DEBUG_TRACE(...)
#define DEBUG_RUN(...)
#endif // JSRUNTIMEHOST_DEBUG_TRACE

namespace Babylon
{
    namespace DebugTrace
    {
        // off by default.
        void EnableDebugTrace(bool enabled);
        // equivalent of a printf that will use TraceOutput function to log. Need DebugTrace to be enabled.
        void Trace(const char* format, ...);
        // set the function used to output.
        void SetTraceOutput(std::function<void(const char* trace)> traceOutputFunction);
    }
}