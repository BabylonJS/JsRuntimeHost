#include "../../../Core/Foundation/Source/StandardStreamLoggerLines.h"
#include <gtest/gtest.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class LineCapture
    {
    public:
        explicit LineCapture(size_t limit)
            : m_limit{limit}
        {
        }

        void Write(const std::string& data, bool flush = false)
        {
            m_pending += data;
            Babylon::StandardStreamLogger::Detail::EmitPendingLines(
                m_pending, m_limit, flush, [this](std::string line) {
                    EXPECT_LE(line.size(), m_limit);
                    Lines.push_back(std::move(line));
                });
        }

        std::string Join() const
        {
            std::string result;
            for (const auto& line : Lines)
            {
                result += line;
            }
            return result;
        }

        std::vector<std::string> Lines;

    private:
        size_t m_limit;
        std::string m_pending;
    };

    constexpr std::array<size_t, 3> Limits{255, 1023, 3800};
}

TEST(StandardStreamLoggerLines, CompleteLineAcrossReadsIsBounded)
{
    LineCapture capture{3800};
    const std::string input = std::string(4095, 'x') + '\n';
    for (size_t offset = 0; offset < input.size(); offset += 1024)
    {
        capture.Write(input.substr(offset, 1024));
    }
    ASSERT_EQ(capture.Lines.size(), 2);
    EXPECT_EQ(capture.Lines[0].size(), 3800);
    EXPECT_EQ(capture.Lines[1].size(), 295);
    EXPECT_EQ(capture.Join(), std::string(4095, 'x'));
}

TEST(StandardStreamLoggerLines, BoundsCompleteAndUnterminatedLinesForEverySink)
{
    for (const auto limit : Limits)
    {
        for (const size_t size : {limit - 1, limit, limit + 1, 2 * limit, 4 * limit + 3})
        {
            for (const bool newline : {false, true})
            {
                SCOPED_TRACE(::testing::Message() << limit << ", " << size << ", " << newline);
                LineCapture capture{limit};
                const std::string content(size, 'x');
                const std::string input = content + (newline ? "\n" : "");
                for (size_t offset = 0; offset < input.size(); offset += 1024)
                {
                    capture.Write(input.substr(offset, 1024));
                }
                capture.Write({}, true);
                EXPECT_EQ(capture.Join(), content);
                EXPECT_EQ(capture.Lines.size(), (size + limit - 1) / limit);
                for (const auto& line : capture.Lines)
                {
                    EXPECT_FALSE(line.empty());
                }
            }
        }
    }
}

TEST(StandardStreamLoggerLines, ExactLimitDoesNotCreateAnExtraEmptyLine)
{
    for (const auto limit : Limits)
    {
        for (const auto* ending : {"\n", "\r\n"})
        {
            LineCapture capture{limit};
            capture.Write(std::string(limit, 'x'));
            for (const char ch : std::string{ending})
            {
                capture.Write(std::string(1, ch));
            }
            capture.Write({}, true);
            ASSERT_EQ(capture.Lines.size(), 1);
            EXPECT_EQ(capture.Lines[0], std::string(limit, 'x'));
        }
        LineCapture capture{limit};
        capture.Write(std::string(limit, 'x') + "\r\n");
        ASSERT_EQ(capture.Lines.size(), 1);
        EXPECT_EQ(capture.Lines[0], std::string(limit, 'x'));
    }
}

TEST(StandardStreamLoggerLines, PreservesBlankLinesAndNormalizesLineEndings)
{
    LineCapture capture{255};
    capture.Write("\nfirst\r");
    capture.Write("\n\nsecond\ntail\r", true);
    EXPECT_EQ(capture.Lines, (std::vector<std::string>{"", "first", "", "second", "tail"}));
}

TEST(StandardStreamLoggerLines, DoesNotDropCarriageReturnAtChunkBoundary)
{
    LineCapture capture{255};
    const std::string input = std::string(254, 'x') + "\rY";
    capture.Write(input + "\n");
    EXPECT_EQ(capture.Join(), input);
}

TEST(StandardStreamLoggerLines, PreservesUtf8AcrossChunkAndReadBoundaries)
{
    for (const auto limit : Limits)
    {
        for (const auto* sequence : {"\xC2\xA9", "\xE2\x98\x83", "\xF0\x9F\x98\x80"})
        {
            LineCapture capture{limit};
            const std::string content = std::string(limit - 1, 'x') + sequence + "tail";
            for (size_t offset = 0; offset < content.size(); offset += limit)
            {
                capture.Write(content.substr(offset, limit));
            }
            capture.Write("\n");
            ASSERT_EQ(capture.Lines.size(), 2);
            EXPECT_EQ(capture.Lines[0], std::string(limit - 1, 'x'));
            EXPECT_EQ(capture.Lines[1], std::string{sequence} + "tail");
            EXPECT_EQ(capture.Join(), content);
        }
    }
}

TEST(StandardStreamLoggerLines, InvalidUtf8StillMakesProgress)
{
    LineCapture capture{255};
    const std::string content(1024, '\x80');
    capture.Write(content + "\n");
    EXPECT_EQ(capture.Join(), content);
}

TEST(StandardStreamLoggerLines, EmptyStreamProducesNoLines)
{
    LineCapture capture{255};
    capture.Write({}, true);
    EXPECT_TRUE(capture.Lines.empty());
}
