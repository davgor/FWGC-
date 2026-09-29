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
#include <set>
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

    // The same, said by a speaker who said `recent` lately (the latest last). An answer whose every
    // place, nest, person and piece of work the stranger heard in `recent` is said again as told
    // (its situation's `_told` lines, and only those: "surroundings_told"), and small talk leaves out
    // a nest the speaker warned of lately.
    std::vector<std::string> replies(const orchestrator::FactPacket& packet, const std::vector<std::string>& recent) const;

    // One of replies(), by what the speaker remembers: each speaker starts at a line of their own,
    // and says the next each time they have heard the same act before (the Memory fact's
    // `heard`), so the same packet gets the same line and a farewell said again gets another.
    // With `meaning`, small talk goes by what `line` (the stranger's words) is about: the
    // weather's lines for the weather, the rest for the speaker's own day, and news with the news
    // (news_asked()); and someone met before
    // who asks to be remembered is greeted with a line that recalls the meeting. `recent` is what this
    // speaker said lately, the latest last, as the game keeps it: a line in it is not said again
    // while one that is not fits, and when every one is, the one said longest ago is (a line said
    // with other passing words, another saying or "some weeks ago" for "a few days ago", is the
    // same line); an answer the stranger heard all of lately is said as told; and small talk does
    // not warn again of a nest the speaker has warned of lately. A line that claims what the
    // speaker has no ground for (truth.hpp: untrue()) is not said: the next one is, then the next
    // bank's. Empty when no line fits.
    std::string speak(const orchestrator::FactPacket& packet, std::string_view line, const std::vector<std::string>& recent = {},
                      Meaning* meaning = nullptr) const;

    // Whether speak() says `packet`'s answer again as told, having said all of it in `recent`.
    bool told(const orchestrator::FactPacket& packet, const std::vector<std::string>& recent) const;

    // What a speaker says before an answer: a bystander who joined unasked, one of their bank's
    // `joined_lead` lines (contract version 10); a frightened speaker, a `panicked_lead` or
    // `afraid_lead` line (9). speak() puts it first. Empty for anyone else, for any outcome but an
    // answer, and before "is it safe", whose verdict opens its answer: an outcome's own opening
    // always comes first.
    std::string lead(const orchestrator::FactPacket& packet, const std::vector<std::string>& recent = {}) const;

    // Every lead lead() could choose from for `packet`, filled, as replies() lists every reply: for
    // the rules test and the eval, which hold each line to the rules whether or not it is true
    // here.
    std::vector<std::string> leads(const orchestrator::FactPacket& packet) const;

    // What a speaker says after their reply (contract version 11): news they have heard. What the
    // stranger did, as their deed (a `heard_deed` line, "You're the one who {deed}"), after a
    // greeting, small talk or an answer about a place or a person; what others did, after small talk
    // or such an answer (a `news` line, "The word is {news}."). speak() puts it last, but never after
    // a reply that asks the stranger something. Empty when the packet carries no news the speaker
    // would give (news_to_pass_on), for a frightened or a cold speaker, and for news told in
    // `recent` in any of its forms. After a greeting, how the speaker's own work went comes first (an
    // `account` line, contract version 20), then a question of theirs never answered (`asked_again`, 21).
    // With no news to tell, a greeting to someone met before may end
    // with the stranger's own words said back (a `recalled` line, contract version 12); with
    // `meaning`, as what they were: a loss (`recalled_grief`), a hardship (`recalled_hardship`), or
    // fear they have come through, their work done (`recalled_doubt`). At a first meeting (contract
    // version 24), a greeting from someone who heard the stranger's name ends with it (a `named` line,
    // before any news), and one from a hero who knows none asks it (an `ask_name` line, after any news),
    // unless `line`, the stranger's words, gave it.
    std::string follow(const orchestrator::FactPacket& packet, const std::vector<std::string>& recent = {},
                       Meaning* meaning = nullptr, std::string_view line = {}) const;

    // Every line follow() could choose from, filled, as leads() lists every lead.
    std::vector<std::string> follows(const orchestrator::FactPacket& packet) const;

    // Every line speak() could choose from for small talk that asks for news (read by meaning), as
    // leads() lists every lead: news not told lately, a `heard_deed` or `news` line as follow() would
    // choose, said alone; told lately in `recent`, others' news as a `news_told` line ({news}) and
    // the stranger's deed as a `heard_deed_told` line, the talk of it; none heard, a `no_news` line.
    // Empty for a frightened or a cold speaker, whose small talk is their own, and for any outcome
    // but small talk.
    std::vector<std::string> news_asked(const orchestrator::FactPacket& packet, const std::vector<std::string>& recent = {}) const;

    // How many lines were read.
    std::size_t size() const;

private:
    // A reply, the situation its line was written for, and whether it recalls a meeting: says
    // how long since ({since}) or what was asked then ({asked}).
    struct Spoken {
        std::string situation;
        std::string text;
        bool recalls = false;
        // The text before and after its passing words (a saying, how long since, what was asked,
        // the season, the time of day), when it has some: said again with others, it is the same
        // line. Without any, `stem` is the whole text.
        std::string stem;
        std::string tail;
        // The blanks it says that make a line fuller ({nearest}, {worst}...): what it tells.
        std::set<std::string> says;
    };

    // replies(), each as Spoken. With `recent` (what the speaker said lately), small talk leaves
    // out a warning of a nest the speaker has just given. `checked`, as speak() says them: a line
    // untrue() finds a claim in is left out, and a bank with none left gives way to the next.
    // `only`, when given, the situations in place of the packet's own (a lead's).
    std::vector<Spoken> spoken_for(const orchestrator::FactPacket& packet, const std::vector<std::string>* recent = nullptr,
                                   bool checked = false, const std::vector<std::string>* only = nullptr) const;

    // How the speaker's own work went (contract version 20): an `account` line, or `account_heard` for work
    // not done beside them, of whichever bank has one, its {account} C++'s words from the Account fact.
    // Empty without one, for a frightened or a cold speaker, and while the speaker said it lately
    // (`recent`). `checked` as for spoken_for().
    std::vector<Spoken> account_lines(const orchestrator::FactPacket& packet, const std::vector<std::string>* recent,
                                      bool checked) const;

    // A question of the speaker's the stranger never answered, asked again after a greeting (contract version
    // 21): an `asked_again` line of whichever bank has one, its {question} the Recall fact's words, whole.
    // Empty without one, for any outcome but a greeting, for a frightened or a cold speaker, and while the
    // speaker asked it lately (`recent`). `checked` as for spoken_for().
    std::vector<Spoken> asked_again_lines(const orchestrator::FactPacket& packet, const std::vector<std::string>* recent,
                                          bool checked) const;

    // news_asked(), each as Spoken; `checked` as for spoken_for().
    std::vector<Spoken> asked_for_news(const orchestrator::FactPacket& packet, const std::vector<std::string>& recent,
                                       bool checked) const;

    // follow() before a first meeting's name: what the speaker says after their reply of their own work,
    // their question, news, or the stranger's words.
    std::string follow_on(const orchestrator::FactPacket& packet, const std::vector<std::string>& recent, Meaning* meaning) const;

    // A first meeting's name (contract version 24), after a greeting on its first turn from a speaker neither
    // cold nor frightened: `named` lines when they know the stranger's name, else `ask_name` lines from a hero
    // (a voice key) when `line` gave none. `checked` as for spoken_for().
    std::vector<Spoken> first_meeting_lines(const orchestrator::FactPacket& packet, const std::vector<std::string>* recent,
                                            bool checked, std::string_view line) const;

    // Adds the rows of `text` to the banks; `source` names it in an error.
    void read(std::string_view text, const std::string& source);

    // The one situation of an outcome that has no chain, or empty.
    static std::string single_situation(const orchestrator::FactPacket& packet);

    // voice -> situation -> lines
    std::map<std::string, std::map<std::string, std::vector<std::string>>> banks_;
};

}  // namespace communicator
