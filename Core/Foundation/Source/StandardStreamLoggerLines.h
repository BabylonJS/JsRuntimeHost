#pragma once

#include <cassert>
#include <cstddef>
#include <string>

namespace Babylon::StandardStreamLogger::Detail
{
    template<typename Emit>
    void EmitPendingLines(std::string& pending, size_t maxLineSize, bool flush, Emit&& emit)
    {
        assert(maxLineSize >= 4);
        size_t start{};
        while (start < pending.size())
        {
            const size_t newline = pending.find('\n', start);
            const size_t end = newline == std::string::npos ? pending.size() : newline;
            size_t size = end - start;
            if (size != 0 && pending[end - 1] == '\r')
            {
                if (newline != std::string::npos || flush)
                {
                    --size;
                }
                else if (size == maxLineSize + 1)
                {
                    // The next read may complete CRLF after an exactly full line.
                    break;
                }
            }
            if (size > maxLineSize)
            {
                size = maxLineSize;
                while (size != 0 && (static_cast<unsigned char>(pending[start + size]) & 0xC0) == 0x80)
                {
                    --size;
                }
                // Standard streams can contain invalid UTF-8; still make progress.
                if (size == 0)
                {
                    size = maxLineSize;
                }
                emit(pending.substr(start, size));
                start += size;
            }
            else if (newline != std::string::npos || flush)
            {
                emit(pending.substr(start, size));
                start = end + (newline != std::string::npos ? 1 : 0);
            }
            else
            {
                break;
            }
        }
        pending.erase(0, start);
    }
}
