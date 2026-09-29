#include "communicator/reaction.hpp"

#include <algorithm>

#include "communicator/error.hpp"
#include "gbnf.hpp"
#include "text.hpp"

namespace communicator {

namespace {

using orchestrator::Reaction;
using orchestrator::ReactionBrief;

std::string joined(const std::vector<std::string>& items, const std::string& between) {
    std::string out;
    for (const std::string& item : items) out += (out.empty() ? "" : between) + item;
    return out;
}

std::vector<std::string> names_of(const std::vector<Reaction>& open) {
    std::vector<std::string> out;
    for (const Reaction reaction : open) out.emplace_back(orchestrator::to_string(reaction));
    return out;
}

// The hero as the brief has them, and the deed, as the model is shown them.
std::string told(const ReactionBrief& b) {
    std::string archetype = text::one_line(b.archetype);
    std::replace(archetype.begin(), archetype.end(), '_', ' ');
    std::vector<std::string> never;
    for (const std::string& n : b.never) never.push_back(text::one_line(n));
    std::string out = "The hero: " + text::one_line(b.name) + ", a " + archetype + " (" + text::one_line(b.alignment) + ").\n";
    if (!b.manner.empty()) out += "How they carry themselves: " + text::one_line(b.manner) + "\n";
    if (!b.voice.empty()) out += "How they speak: " + text::one_line(b.voice) + "\n";
    if (!b.toward_player.empty()) out += "With strangers: " + text::one_line(b.toward_player) + "\n";
    if (!b.line.empty()) out += "A line of theirs: \"" + text::one_line(b.line) + "\"\n";
    if (!never.empty()) out += "They would never: " + joined(never, "; ") + "\n";
    out += "What they did: " + text::one_line(b.deed) + "\n";
    out += std::string("How it went: they ") + (b.outcome == "won" ? "won" : "lost") + ".\n";
    return out + "How do they take it? One of: " + joined(names_of(b.open), ", ") + ".";
}

}  // namespace

ReactionPicker::ReactionPicker(inference::Model& model, PickOptions options) : model_(model), options_(options) {
    if (options_.max_tokens < 4 || options_.max_tokens > 64) throw Error("a pick's max_tokens is 4 to 64");
}

std::string ReactionPicker::grammar(const ReactionBrief& brief) {
    return "root ::= " + gbnf::one_of(names_of(brief.open)) + "\n";
}

std::vector<inference::Message> ReactionPicker::messages(const ReactionBrief& brief) {
    std::vector<inference::Message> out;
    out.push_back({"system",
                   "You judge how a hero of a fantasy world takes what befell them, from who they are. You are shown the "
                   "hero -- how they carry themselves, how they speak, how they are with strangers, a line of theirs, what "
                   "they would never do -- then what they did and how it went. Answer with one word, of those offered:\n"
                   "- humbled: it takes them down a peg; they own their part in it and carry it quietly.\n"
                   "- embittered: it hardens them; they blame others or the world, and grow colder.\n"
                   "- resolved: it steels them; they mean to learn from it and do better.\n"
                   "- grieving: it breaks their heart; they mourn what was lost and need time.\n"
                   "- vengeful: they want the foe that beat them, and mean to go after it.\n"
                   "- vindicated: it proves them right; they grow surer of themselves and their way.\n"
                   "Judge by the hero's character, not by what would be wisest."});
    // Worked examples, as chat turns, each answered differently: a model read the answer itself from two, every win
    // vindicated and most losses humbled as they were; and from none, nearly everything resolved (react.txt,
    // 2026-09-29).
    out.push_back({"user",
                   "The hero: Orsa, a sworn guardian (Lawful Good).\n"
                   "How they carry themselves: measured and dutiful, slow to speak of themselves\n"
                   "How they speak: plain words, the duty before the reason\n"
                   "With strangers: correct, and kind when it costs nothing\n"
                   "A line of theirs: \"The post is kept. That is all there is to say.\"\n"
                   "They would never: leave a post unmanned\n"
                   "What they did: Held the bridge at Hollin against the marsh trolls.\n"
                   "How it went: they won.\n"
                   "How do they take it? One of: resolved, vindicated."});
    out.push_back({"assistant", "resolved"});
    out.push_back({"user",
                   "The hero: Karrag, a raider chief (Chaotic Evil).\n"
                   "How they carry themselves: loud, sure of their luck, quick to take offence\n"
                   "How they speak: boasts and threats\n"
                   "With strangers: sizing them up\n"
                   "A line of theirs: \"Nobody takes from me and keeps it.\"\n"
                   "They would never: let an insult stand\n"
                   "What they did: Was driven from the ford at Ashby by the grey wolves.\n"
                   "How it went: they lost.\n"
                   "How do they take it? One of: humbled, embittered, resolved, grieving, vengeful."});
    out.push_back({"assistant", "vengeful"});
    out.push_back({"user",
                   "The hero: Maelis, a village healer who took up arms (Neutral Good).\n"
                   "How they carry themselves: gentle, worn thin by the years\n"
                   "How they speak: softly, of the people they tend\n"
                   "With strangers: warm, and quick to worry for them\n"
                   "A line of theirs: \"Everyone here is someone's child.\"\n"
                   "They would never: leave the hurt untended\n"
                   "What they did: Fought the ghouls at the mill at Tarn, and lost.\n"
                   "How it went: they lost.\n"
                   "How do they take it? One of: humbled, embittered, resolved, grieving, vengeful."});
    out.push_back({"assistant", "grieving"});
    out.push_back({"user", told(brief)});
    return out;
}

orchestrator::Reaction ReactionPicker::pick(const ReactionBrief& brief) const {
    if (brief.open.empty()) throw Error("a brief with no reaction open has nothing to pick");
    for (const std::string* field : {&brief.name, &brief.archetype, &brief.alignment, &brief.manner, &brief.voice,
                                     &brief.toward_player, &brief.line, &brief.deed})
        text::check_line(*field);
    for (const std::string& never : brief.never) text::check_line(never);
    inference::Sampling sampling;
    sampling.max_tokens = options_.max_tokens;
    sampling.temperature = 0.0f;
    sampling.grammar = grammar(brief);
    const std::string said = model_.complete(model_.chat_prompt(messages(brief)), sampling).text;
    const std::optional<Reaction> reaction = orchestrator::reaction_named(said);
    // The grammar allows nothing else; a model that stopped short of a whole word is the one way here.
    if (!reaction || std::find(brief.open.begin(), brief.open.end(), *reaction) == brief.open.end())
        throw Error("the picker answered what is not open: '" + said + "'");
    return *reaction;
}

}  // namespace communicator
