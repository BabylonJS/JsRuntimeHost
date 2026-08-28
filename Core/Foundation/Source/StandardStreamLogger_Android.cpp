#include "StandardStreamLoggerPlatform.h"

#include <android/log.h>
#include <string>

// Android mirrors drained lines to logcat. The tee/redirect machinery is shared
// with Apple via StandardStreamLogger_Posix.inl.
#define SSL_WRITE_PLATFORM(stream, line)                                      \
    do                                                                        \
    {                                                                         \
        const int priority = (stream) == Stream::Error                        \
            ? ANDROID_LOG_ERROR                                               \
            : ANDROID_LOG_INFO;                                               \
        __android_log_write(priority, "JsRuntimeHost", (line).c_str());       \
    } while (0)

#include "StandardStreamLogger_Posix.inl"