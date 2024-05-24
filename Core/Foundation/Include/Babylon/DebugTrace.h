#pragma once
#include <functional>

#define DEBUG_TRACE_NOOP(...)

#define _STRINGIFY(x) #x
#define _TOSTRING(x) _STRINGIFY(x)
#define _DEBUG_TRACE(_format, ...) Babylon::DebugTrace::Trace(__FILE__ "(" _TOSTRING(__LINE__) "): " _format "\n", ##__VA_ARGS__); 

#if JSRUNTIMEHOST_DEBUG_TRACE
#   define DEBUG_TRACE _DEBUG_TRACE
#else
#   define DEBUG_TRACE(...) DEBUG_TRACE_NOOP()
#endif // JSRUNTIMEHOST_DEBUG_TRACE

namespace Babylon
{
    namespace DebugTrace
    {
        // off by default.
        void SetDebugTrace(bool enabled);
        // equivalent of a printf that will use TraceOutput function to log. Need DebugTrace to be enabled.
        void Trace(const char* _format, ...);
        // set the function used to output.
        void SetTraceOutput(std::function<void(const char* trace)> traceOutputFunction);
    }
}