#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Babylon::StandardStreamLogger::Detail
{
    // Windows UTF-16 text descriptors still tee their original bytes; only the
    // diagnostic copy is decoded. Retain incomplete code units/pairs between reads.
    inline void AppendUtf16LE(std::string& output, std::string& pending, bool flush)
    {
        const auto codeUnit = [&pending](size_t offset) -> uint32_t {
            return static_cast<unsigned char>(pending[offset]) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(pending[offset + 1])) << 8);
        };
        size_t offset{};
        while (offset + 1 < pending.size())
        {
            uint32_t value = codeUnit(offset);
            size_t consumed{2};
            if (value >= 0xD800 && value <= 0xDBFF)
            {
                if (offset + 3 >= pending.size())
                {
                    if (!flush)
                    {
                        break;
                    }
                    value = 0xFFFD;
                    consumed = pending.size() - offset;
                }
                else if (codeUnit(offset + 2) >= 0xDC00 && codeUnit(offset + 2) <= 0xDFFF)
                {
                    value = 0x10000 + ((value - 0xD800) << 10) + (codeUnit(offset + 2) - 0xDC00);
                    consumed = 4;
                }
                else
                {
                    value = 0xFFFD;
                }
            }
            else if (value >= 0xDC00 && value <= 0xDFFF)
            {
                value = 0xFFFD;
            }
            offset += consumed;

            if (value <= 0x7F)
            {
                output.push_back(static_cast<char>(value));
            }
            else if (value <= 0x7FF)
            {
                output.push_back(static_cast<char>(0xC0 | (value >> 6)));
                output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
            else
            {
                if (value > 0xFFFF)
                {
                    output.push_back(static_cast<char>(0xF0 | (value >> 18)));
                    output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
                }
                else
                {
                    output.push_back(static_cast<char>(0xE0 | (value >> 12)));
                }
                output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
        }
        if (flush && offset < pending.size())
        {
            output.append("\xEF\xBF\xBD");
            offset = pending.size();
        }
        pending.erase(0, offset);
    }

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
