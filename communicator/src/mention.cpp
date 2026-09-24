#include "mention.hpp"

#include <string>
#include <vector>

#include "text.hpp"

namespace communicator {

namespace {

// `text` as lower-case words between spaces, " like this ", so a search for " a b " finds
// whole words only. Letters and digits make words; anything else parts them.
std::string spaced_words(std::string_view text) {
    std::string out = " ";
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u >= 0x80) {
            out += c;
        } else if (u >= 'A' && u <= 'Z') {
            out += static_cast<char>(u - 'A' + 'a');
        } else if (out.back() != ' ') {
            out += ' ';
        }
    }
    if (out.back() != ' ') out += ' ';
    return out;
}

}  // namespace

std::optional<Referent> named_outright(Kind kind, const Known& known, std::string_view line) {
    const std::string said = spaced_words(text::plain_punctuation(line));
    std::optional<Referent> best;
    std::size_t best_length = 0;
    bool tied = false;
    for (const Referent& r : known.nameable(kind, Known::kKeptPerKind)) {
        const std::string name = spaced_words(text::spoken(r.label));  // a name as said: "Hakndum (Age 2)" is "Hakndum"
        if (name.size() <= 2 || said.find(name) == std::string::npos) continue;  // " " alone names nothing
        if (name.size() > best_length) {
            best = r;
            best_length = name.size();
            tied = false;
        } else if (name.size() == best_length) {
            tied = true;
        }
    }
    return tied ? std::nullopt : best;
}

}  // namespace communicator
