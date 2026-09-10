#pragma once

#include <cstdint>
#include <limits>

namespace Babylon::Internal
{
    constexpr int32_t IncrementTimerId(int32_t id)
    {
        return id == std::numeric_limits<int32_t>::max() ? 1 : id + 1;
    }
}
