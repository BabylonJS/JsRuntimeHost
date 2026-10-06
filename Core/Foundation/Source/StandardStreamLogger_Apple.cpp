#include "StandardStreamLoggerPlatform.h"

#include <os/log.h>
#include <cerrno>
#include <cstdint>
#include <string>

#include <fcntl.h>
#include <unistd.h>

namespace
{
// POSIX fd helpers (dup/pipe/CLOEXEC/devnull); sink is OsWritePlatform below.
#include "StandardStreamLogger_PosixOps.inl"

    size_t OsMaxPlatformLineSize(bool isError)
    {
        // Reserve the terminator within os_log's persisted dynamic-content budget.
        return isError ? 255 : 1023;
    }

    void OsWritePlatform(bool isError, const std::string& line)
    {
        const os_log_type_t type = isError ? OS_LOG_TYPE_ERROR : OS_LOG_TYPE_DEFAULT;
        os_log_with_type(OS_LOG_DEFAULT, type, "%{public}s", line.c_str());
    }
}

#include "StandardStreamLogger_Shared.inl"