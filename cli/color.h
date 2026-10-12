#pragma once

#include <cstring>
#include <streambuf>
#include <string>

// Color at the keyboard. Off unless the terminal takes ANSI escapes, the run is
// not --json, and NO_COLOR is unset (https://no-color.org): a pipe, a log file
// or a caller's parser must never see an escape code. stdout and stderr are
// decided apart, since either can be redirected alone.
namespace ewr::cli
{
    namespace ansi
    {
        inline constexpr const char* kReset = "\033[0m";
        inline constexpr const char* kBold = "\033[1m";
        inline constexpr const char* kRed = "\033[91m";
        inline constexpr const char* kGreen = "\033[92m";
        inline constexpr const char* kYellow = "\033[93m";
        inline constexpr const char* kBlue = "\033[94m";
        inline constexpr const char* kMagenta = "\033[95m";
        inline constexpr const char* kCyan = "\033[96m";
        // Bright black, not faint: older Windows consoles ignore faint.
        inline constexpr const char* kGray = "\033[90m";
        inline constexpr const char* kDarkCyan = "\033[36m";
        inline constexpr const char* kDarkMagenta = "\033[35m";
        inline constexpr const char* kDarkYellow = "\033[33m";
    }

    // Green at 0 %, yellow at 50 %, red at 100 %, in eleven steps of the
    // 256-color palette, which every terminal that takes color here shows -
    // unlike 24-bit color, which macOS Terminal does not.
    inline const char* RampColor(int percent)
    {
        static const char* const kSteps[] = {
            "\033[38;5;46m",  "\033[38;5;82m",  "\033[38;5;118m", "\033[38;5;154m",
            "\033[38;5;190m", "\033[38;5;226m", "\033[38;5;220m", "\033[38;5;214m",
            "\033[38;5;208m", "\033[38;5;202m", "\033[38;5;196m",
        };

        if (percent < 0)
            percent = 0;
        if (percent > 100)
            percent = 100;
        return kSteps[(percent + 5) / 10];
    }

    struct ColorState
    {
        bool out{ false };
        bool err{ false };
    };

    inline ColorState& Colors()
    {
        static ColorState state;
        return state;
    }

    inline std::string Paint(const std::string& text, const char* color, bool on)
    {
        if (!on || !color || text.empty())
            return text;
        return std::string(color) + text + ansi::kReset;
    }

    // For text bound for stdout.
    inline std::string Paint(const std::string& text, const char* color)
    {
        return Paint(text, color, Colors().out);
    }

    // For text bound for stderr.
    inline std::string PaintErr(const std::string& text, const char* color)
    {
        return Paint(text, color, Colors().err);
    }

    // The tags that open a line of output, CLI and library alike. Anything else
    // in brackets - a menu's "[1]" - is left alone.
    inline const char* TagColor(const std::string& tag)
    {
        if (tag == "[ERROR]" || tag == "[FATAL]")
            return ansi::kRed;
        if (tag == "[!]" || tag == "[WARNING]" || tag == "[-]")
            return ansi::kYellow;
        if (tag == "[SUCCESS]" || tag == "[+]")
            return ansi::kGreen;
        if (tag == "[i]")
            return ansi::kCyan;
        if (tag == "[*]")
            return ansi::kBlue;
        if (tag == "[DRY RUN]")
            return ansi::kMagenta;
        if (tag == "[RESULT]")
            return ansi::kBold;
        return nullptr;
    }

    // Paints the tag a line opens with, so every message is colored without
    // touching the hundreds of places that print one. A line starts after '\n'
    // or '\r' (the in-place write progress) and may be indented. Text is held
    // until a newline or a flush: a tag can arrive in pieces, and a prompt ends
    // without a newline but is flushed before the read.
    class TagColorBuf final : public std::streambuf
    {
    public:
        explicit TagColorBuf(std::streambuf* inner) : inner_(inner) {}

    protected:
        int overflow(int ch) override
        {
            if (ch == traits_type::eof())
                return traits_type::not_eof(ch);

            pending_ += static_cast<char>(ch);
            if (ch == '\n' && !EmitLines())
                return traits_type::eof();
            return ch;
        }

        std::streamsize xsputn(const char* s, std::streamsize n) override
        {
            pending_.append(s, static_cast<std::size_t>(n));
            if (std::memchr(s, '\n', static_cast<std::size_t>(n)) && !EmitLines())
                return 0;
            return n;
        }

        int sync() override
        {
            return (Emit(pending_.size()) && inner_->pubsync() == 0) ? 0 : -1;
        }

    private:
        // Up to the last newline; the unfinished line waits for its tag.
        bool EmitLines()
        {
            const std::size_t last = pending_.rfind('\n');
            return last == std::string::npos || Emit(last + 1);
        }

        bool Emit(std::size_t count)
        {
            std::string out;
            out.reserve(count + 16);

            for (std::size_t i = 0; i < count; ++i)
            {
                const char c = pending_[i];

                if (lineStart_ && c == '[')
                {
                    const std::size_t close = pending_.find(']', i);
                    if (close < count && close - i < 12)
                    {
                        const std::string tag = pending_.substr(i, close - i + 1);
                        if (const char* color = TagColor(tag))
                        {
                            out += Paint(tag, color, true);
                            i = close;
                            lineStart_ = false;
                            continue;
                        }
                    }
                }

                out += c;
                if (c == '\n' || c == '\r')
                    lineStart_ = true;
                else if (c != ' ')
                    lineStart_ = false;
            }

            pending_.erase(0, count);
            const std::streamsize size = static_cast<std::streamsize>(out.size());
            return inner_->sputn(out.data(), size) == size;
        }

        std::streambuf* inner_;
        std::string pending_;
        bool lineStart_{ true };
    };

} // namespace ewr::cli
