#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <functional>

namespace Babylon
{
    namespace
    {
        static bool s_debugTraceEnabled{};
        static std::function<void(const char* output)> s_traceOutputFunction;
        static void TraceArgs(const char* format, va_list args)
        {
            char temp[8192];
            auto len = vsnprintf(temp, sizeof(temp), format, args);
            temp[len] = '\0';
            s_traceOutputFunction(temp);
        }
    }

    namespace DebugTrace
    {
        void SetDebugTrace(bool enabled)
        {
            s_debugTraceEnabled = enabled;
        }

        void Trace(const char* format, ...)
        {
            if (!(s_debugTraceEnabled && s_traceOutputFunction))
            {
                return;
            }
            va_list args;
            va_start(args, format);
            TraceArgs(format, args);
            va_end(args);
        }

        void SetTraceOutput(std::function<void(const char* trace)> traceOutputFunction)
        {
            s_traceOutputFunction = std::move(traceOutputFunction);
        }
    }
}