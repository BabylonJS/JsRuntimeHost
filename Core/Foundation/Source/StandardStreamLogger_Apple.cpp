#include "StandardStreamLoggerPlatform.h"

#include <os/log.h>
#include <string>

// Apple mirrors drained lines to os_log. The tee/redirect machinery is shared
// with Android via StandardStreamLogger_Posix.inl.
#define SSL_WRITE_PLATFORM(stream, line)                                      \
    do                                                                        \
    {                                                                         \
        const os_log_type_t type = (stream) == Stream::Error                  \
            ? OS_LOG_TYPE_ERROR                                               \
            : OS_LOG_TYPE_DEFAULT;                                            \
        os_log_with_type(OS_LOG_DEFAULT, type, "%{public}s", (line).c_str()); \
    } while (0)

#include "StandardStreamLogger_Posix.inl"