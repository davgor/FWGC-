// Written lines, spoken for a fact packet.
//
// A writer's line has blanks ({giver}, {nearest}...) that C++ fills from the packet. A reply so
// says only what its packet holds, in words a person chose, and no model writes it. Lines come in
// banks, one per voice:
// - a hero's archetype and alignment ("warden/LN", the persona's voice_key);
// - a roster post ("post:innkeeper");
// - "any", for everyone.
// A speaker is answered from their own bank first, then their archetype's true-neutral bank
// ("warden/NN", where the archetype shows almost unmodified) when their own alignment has none,
// then their post's, then "any": the first that has a line for the situation which the packet
// can fill. Of those lines, the ones that say the most of the packet are said. A part of a line
// in [brackets] is optional: it is left out whole when the packet cannot fill a blank in it.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "orchestrator/contract.hpp"

namespace communicator {

class Meaning;

class Lines {
public:
    // Reads `path`, a file or a folder of `.txt` files (read in name order): rows of
    // `voice | situation | line`, cells trimmed, `#` starting a comment. Throws Error for a row
    // that is not three cells, a situation situations() does not know, a line with a blank its
    // situation does not have or a bad optional part, or a path that cannot be read.
    static Lines load(const std::string& path);

    // The same, from text already read; `source` names it in an error.
    static Lines parse(std::string_view text, const std::string& source);

    // Every situation a line may be written for, and the blanks each may use.
    static const std::map<std::string, std::vector<std::string>>& situations();

    // The situations a packet puts its speaker in, the closest first: a warm greeting to someone
    // met before is "greeted_warm", then "greeted_again". A bank answers from the first of them
    // it has a line for. Empty when none fits (an answer with no facts to give, say). Each chain
    // stays inside one reply shape (reply_shape()), so a line written for any situation in it
    // opens as the packet's outcome does.
    static std::vector<std::string> situations_for(const orchestrator::FactPacket& packet);

    // The closest of situations_for(), or empty.
    static std::string situation(const orchestrator::FactPacket& packet);

    // Every reply the lines give `packet`, from the first bank that has one: the lines that say
    // the most of it, filled, each ending in the closing C++ writes for a refusal (reply_shape()).
    // Empty when no line fits.
    std::vector<std::string> replies(const orchestrator::FactPacket& packet) const;

    // One of replies(), by what the speaker remembers: each speaker starts at a line of their own,
    // and says the next each time they have heard the same act before (the Memory fact's
    // `heard`), so the same packet gets the same line and a farewell said again gets another.
    // With `meaning`, small talk goes by what `line` (the stranger's words) is about: the
    // weather's lines for the weather, the rest for the speaker's own day. `recent` is what this
    // speaker said lately, the latest last, as the game keeps it: a line in it is not said again
    // while one that is not fits, and when every one is, the one said longest ago is. Empty when
    // no line fits.
    std::string speak(const orchestrator::FactPacket& packet, std::string_view line, const std::vector<std::string>& recent = {},
                      Meaning* meaning = nullptr) const;

    // How many lines were read.
    std::size_t size() const;

private:
    // replies(), each with the situation its line was written for.
    std::vector<std::pair<std::string, std::string>> spoken_for(const orchestrator::FactPacket& packet) const;

    // Adds the rows of `text` to the banks; `source` names it in an error.
    void read(std::string_view text, const std::string& source);

    // The one situation of an outcome that has no chain, or empty.
    static std::string single_situation(const orchestrator::FactPacket& packet);

    // voice -> situation -> lines
    std::map<std::string, std::map<std::string, std::vector<std::string>>> banks_;
};

}  // namespace communicator
