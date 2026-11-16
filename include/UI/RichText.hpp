#pragma once

#include <string>
#include <string_view>

namespace StandaloneStreamChat::UI {
    /// Chat text (and anything else viewers control) is untrusted: makes it
    /// show literally in a rich-text TMP label instead of being read as tags.
    ///
    /// Wrapping the whole text in one <noparse> block is not enough. TMP ends
    /// a tag name at '=' or a space ("</noparse=1>" closes the block), matches
    /// names by a 32-bit hash that collides easily, pre-parses <br> and
    /// <style> without regard to <noparse> (TMP 3.0), and turns "<" into
    /// '<' before parsing tags (TMP 4.0). So no '<' or '\' from the text is
    /// left where TMP could read what follows it: each one is drawn from its
    /// own tiny <noparse> block, which puts the '<' of "</noparse>" right
    /// after it. TMP abandons a tag scan at the next '<', and no backslash
    /// escape starts with '<'. The rest of the text holds neither character,
    /// so it can never start a tag or an escape.
    ///
    /// Control characters and the Unicode line and paragraph separators
    /// become spaces, so a message can't stack line breaks to fill the panel.
    inline std::string Escape(std::string_view text) {
        static constexpr std::string_view LessThan = "<noparse><</noparse>";
        static constexpr std::string_view Backslash = "<noparse>\\</noparse>";
        std::string out;
        out.reserve(text.size() + 16);
        for (size_t i = 0; i < text.size(); i++) {
            auto c = static_cast<unsigned char>(text[i]);
            if (c == '<') {
                out += LessThan;
            } else if (c == '\\') {
                out += Backslash;
            } else if (c < 0x20 || c == 0x7F) {
                out.push_back(' ');
            } else if (c == 0xE2 && i + 2 < text.size() && text[i + 1] == '\x80' && (text[i + 2] == '\xA8' || text[i + 2] == '\xA9')) {
                out.push_back(' ');  // U+2028 LINE SEPARATOR, U+2029 PARAGRAPH SEPARATOR
                i += 2;
            } else {
                out.push_back(text[i]);
            }
        }
        return out;
    }
}
