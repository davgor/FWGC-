#include "communicator/interpreter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <set>

#include "communicator/error.hpp"
#include "gbnf.hpp"
#include "mention.hpp"
#include "text.hpp"

namespace communicator {

namespace {

using orchestrator::ActKind;

// A worked example: what was said last (if anything) and the traveller's line, from another
// place and time so that nothing in it can be copied as an answer.
struct Example {
    std::string_view last_reply;
    std::string_view line;
};

// Each act as the model is told it, the kind of thing it points at if any, lines that are this
// act, and how the target question puts what the traveller did.
struct ActText {
    ActKind kind;
    std::optional<Kind> target;
    std::string_view meaning;
    std::array<Example, 2> examples;  // up to two; an empty line is none
    std::string_view pointing;         // for the target question: what the line does
    // The target is the line's own words, found by C++ and never asked of the model: the name the
    // player gives (contract version 24, name_given()).
    bool said = false;
};

// In the contract's order (a check below holds it there). The model is shown them in kShown's.
constexpr std::array<ActText, orchestrator::kActCount> kActs = {{
    {ActKind::AskSurroundings, std::nullopt, "asks what is around here, where they are, or the way somewhere",
     {{{"", "What's around here?"}, {"", "Which way to the coast?"}}}, ""},
    {ActKind::AskAboutPlace, Kind::Place, "asks about a place by its name",
     {{{"", "Tell me of Oakmere."}, {"", "What is the old mill like?"}}}, "asks about a place"},
    {ActKind::AskAboutPerson, Kind::Person, "asks about a person by name",
     {{{"", "Who is Tamsin?"}, {"", "Tell me about Old Bram."}}}, "asks about a person"},
    {ActKind::AskForWork, std::nullopt, "asks for work, a job or a task, or who needs help",
     {{{"", "Any work going?"}, {"", "Does anyone need a hand?"}}}, ""},
    {ActKind::AcceptQuest, Kind::Offer, "says yes to work, or agrees to do a job",
     {{{"", "I'll clear the mill for you."}, {"Will you clear the rats from my mill?", "Yes, I'll do it."}}},
     "agrees to take on work"},
    {ActKind::DeclineQuest, Kind::Offer, "says no to work they were offered",
     {{{"Will you clear the rats from my mill?", "No, not for me."}, {"", "I won't clear your mill."}}},
     "turns down work"},
    {ActKind::AbandonQuest, Kind::Held, "gives up a job they took on earlier", {{{"", "I'm done with the mill job."}, {"", ""}}},
     "gives up work they took on"},
    {ActKind::Farewell, std::nullopt, "says goodbye or leaves", {{{"", "Goodbye, and thanks."}, {"", "I must be going."}}}, ""},
    {ActKind::NoAct, std::nullopt, "anything else, which none of the acts above describes",
     {{{"", "Sing me a song."}, {"", "Give me your horse."}}}, ""},
    {ActKind::Greet, std::nullopt, "greets them: hello, good day", {{{"", "Good morning to you!"}, {"", "Hail, friend."}}}, ""},
    {ActKind::SmallTalk, std::nullopt, "talks for the sake of it: how they are, the weather, the day, their trade",
     {{{"", "Fine weather today."}, {"", "How's the trade treating you?"}}}, ""},
    {ActKind::Answer, std::nullopt, "answers a question they were just asked, when it is not about work and they are not leaving",
     {{{"Where are you headed?", "To the coast, if the road holds."}, {"Have you eaten?", "Not since morning."}}}, ""},
    {ActKind::Thank, std::nullopt, "thanks them", {{{"", "Thank you kindly."}, {"", "Much obliged."}}}, ""},
    {ActKind::Insult, std::nullopt, "insults or threatens them", {{{"", "You're a useless old fool."}, {"", "Out of my way or you'll regret it."}}},
     ""},
    {ActKind::AskSafety, std::nullopt, "asks whether it is safe here, or what to beware of",
     {{{"", "Is it safe around here?"}, {"", "What should I watch out for?"}}}, ""},
    {ActKind::Confront, std::nullopt, "tells them they are not what they seem, or accuses them of hiding who they are",
     {{{"", "I know you're no miller."}, {"", "Drop the act. I know what you are."}}}, ""},
    {ActKind::Apologize, std::nullopt, "says sorry for something they did, or asks to be forgiven",
     {{{"", "I'm sorry for what I said."}, {"", "Forgive me, I was out of line."}}}, ""},
    {ActKind::Calm, std::nullopt, "soothes them when they are frightened: it's all right, you're safe, calm down",
     {{{"", "It's all right, you're safe now."}, {"", "Easy, easy. Nobody's going to hurt you."}}}, ""},
    {ActKind::AskAboutThem, std::nullopt,
     "asks about them: who they are, how long they have been here, why, what they are after, what they did or lost",
     {{{"", "How long have you held this place?"}, {"", "What brought you out here?"}}}, ""},
    {ActKind::Complain, std::nullopt, "complains about them or their work: it was worse than they said, they should have warned them",
     {{{"", "That job was far worse than you let on."}, {"", "You should have told me it was that dangerous."}}}, ""},
    {ActKind::Introduce, std::nullopt, "tells them their own name: I'm..., my name is..., call me..., or gives it when asked who they are",
     {{{"", "My name's Hedda, by the way."}, {"And you are?", "Osric."}}}, "", true},
}};

const ActText& text_of(ActKind kind) { return kActs[static_cast<std::size_t>(kind)]; }

// The order the model is shown the acts in: the contract's, with "anything else" last, after
// everything it is not. The acts added later come after the ones first measured, in their order:
// the test model reads the first acts listed far more often, and listing greet and small_talk
// first read 16 of 23 clear lines as one of those two (2026-09-26).
constexpr std::array<ActKind, orchestrator::kActCount> kShown = {
    ActKind::AskSurroundings, ActKind::AskAboutPlace, ActKind::AskAboutPerson, ActKind::AskForWork,
    ActKind::AcceptQuest,     ActKind::DeclineQuest,  ActKind::AbandonQuest,   ActKind::Farewell,
    ActKind::Greet,           ActKind::SmallTalk,     ActKind::Answer,         ActKind::Thank,
    ActKind::Insult,          ActKind::AskSafety,     ActKind::Confront,       ActKind::Apologize,
    ActKind::Calm,            ActKind::AskAboutThem,  ActKind::Complain,       ActKind::Introduce,
    ActKind::NoAct};

// Whether an act can be put to the model: it takes no target, or there is something to point at;
// an answer only to a question, when the speaker's last words asked one; and calming words only to
// a frightened speaker. Offered to everyone, calming took the test model from 112 lines of 310
// read right to 79 of the same, most read as small talk instead (measured 2026-09-26): a listed act
// costs a small model on every line, not only the lines it fits.
// Whether `line` speaks to the one it is said to: "you", "your", "yourself", and the ways players
// write them ("u", "ur", "urself", "ye", "thee"). A question about the speaker does.
bool addresses_them(std::string_view line) {
    static const std::set<std::string> words = {"you", "your", "yours", "yourself", "you're", "you've", "you'd", "you'll", "u",
                                                "ur",  "urself", "ye", "yer", "thee", "thou", "thy", "thine"};
    std::string word;
    for (const char c : std::string(line) + " ") {
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '\'') {
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            if (words.count(word) != 0) return true;
            word.clear();
        }
    }
    return false;
}

bool offered(const ActText& act, const Known& known, std::string_view line, std::string_view last_reply, bool frightened,
             bool worked_for) {
    if (act.kind == ActKind::Answer) return last_reply.find('?') != std::string_view::npos;
    if (act.kind == ActKind::Calm) return frightened;
    // A complaint (contract version 18) is about the speaker's own work, so only once there is some between
    // them: listed on every line, as calm was, it would cost a small model the lines it does not fit.
    if (act.kind == ActKind::Complain) return worked_for;
    // A question about the speaker (contract version 13) speaks to them. Listed on every line, the
    // act cost the test model 9 of the 313 lines it had read right, and Qwen3-4B 5; listed only for a
    // line that speaks to them, none and 1 (110 -> 113, 246 -> 245; 2026-09-28).
    if (act.kind == ActKind::AskAboutThem) return addresses_them(line);
    // A name the player gives (contract version 24), only in a line that gives one.
    if (act.said) return name_given(line, last_reply).has_value();
    // Someone asked of by title or by a name no packet gave (contract version 25), for the director to look up.
    if (act.kind == ActKind::AskAboutPerson && person_asked(line).has_value()) return true;
    return !act.target || !known.nameable(*act.target, 1).empty();
}

// What the traveller says, and what was said to them last, as the model reads it.
std::string exchange(std::string_view line, std::string_view last_reply) {
    std::string out;
    const std::string reply = text::one_line(last_reply);
    if (!reply.empty()) out += "They last said: \"" + reply + "\"\n";
    return out + "The traveller says: \"" + text::one_line(line) + "\"";
}

// The lower-case words of `text` that carry meaning: letters only, a few common words left out.
std::set<std::string> content_words(std::string_view text) {
    static const std::set<std::string> common = {"a",    "an",   "the",  "of",   "to",   "in",  "on",   "at",   "for", "with",
                                                 "and",  "or",   "me",   "my",   "you",  "your", "i",    "it",   "is",  "be",
                                                 "this", "that", "from", "by",   "about", "we",  "our",  "will", "can", "do"};
    std::set<std::string> out;
    std::string word;
    const auto flush = [&] {
        if (!word.empty() && !common.count(word)) out.insert(word);
        word.clear();
    };
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= 'a' && u <= 'z') || u >= 0x80) {
            word += c;
        } else if (u >= 'A' && u <= 'Z') {
            word += static_cast<char>(u - 'A' + 'a');
        } else {
            flush();
        }
    }
    flush();
    return out;
}

// How many things the target question offers: few enough for a small model to weigh.
constexpr std::size_t kCandidates = 6;

// The things of `kind` a player could mean, those sharing the most words with what was said
// first; otherwise as Known orders them. At most kCandidates.
std::vector<Referent> candidates(Kind kind, const Known& known, std::string_view line, std::string_view last_reply) {
    std::vector<Referent> all = known.nameable(kind, Known::kKeptPerKind);
    const std::set<std::string> said = [&] {
        std::set<std::string> words = content_words(text::plain_punctuation(line));
        words.merge(content_words(text::plain_punctuation(last_reply)));
        return words;
    }();
    std::vector<std::pair<std::size_t, Referent>> scored;
    for (Referent& r : all) {
        std::size_t shared = 0;
        for (const std::string& w : content_words(r.label)) shared += said.count(w);
        scored.emplace_back(shared, std::move(r));
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<Referent> out;
    for (auto& [shared, r] : scored) {
        if (out.size() == kCandidates) break;
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace

std::string confirm_question(ActKind kind) {
    if (!orchestrator::act_changes_world(kind)) return {};
    switch (kind) {
        case ActKind::AcceptQuest:
            return "Take on this work?";
        case ActKind::AbandonQuest:
            return "Give up this work?";
        default:
            return "Do this?";  // an act the contract makes world-changing later, until it has words of its own
    }
}

Interpreter::Interpreter(inference::Model& model) : model_(model) {
    // The table must say what the contract says: an act added there, or one whose target
    // changed, stops here rather than being offered to the model wrongly.
    std::set<ActKind> shown;
    for (std::size_t i = 0; i < kActs.size(); ++i) {
        const ActKind kind = static_cast<ActKind>(i);
        if (kActs[i].kind != kind || orchestrator::act_needs_target(kind) != (kActs[i].target.has_value() || kActs[i].said))
            throw Error("the interpreter's act table disagrees with the orchestrator's contract at " +
                        std::string(orchestrator::to_string(kind)));
        shown.insert(kShown[i]);
    }
    if (shown.size() != kActs.size()) throw Error("the interpreter shows the model some act twice, and another never");
}

Question Interpreter::act_question(std::string_view line, const Known& known, std::string_view last_reply, bool frightened,
                                   bool worked_for) const {
    std::string acts, alternatives;
    for (const ActKind kind : kShown) {
        const ActText& act = text_of(kind);
        if (!offered(act, known, line, last_reply, frightened, worked_for)) continue;  // nothing to point at, no question, no fear
        const std::string name(orchestrator::to_string(act.kind));
        acts += "- " + name + ": " + std::string(act.meaning) + ".\n";
        alternatives += (alternatives.empty() ? "" : " | ") + gbnf::literal(name);
    }
    // The worked examples go in as earlier turns: each offered act's first, then each one's
    // second, so no one act's examples all sit next to the line.
    std::vector<std::pair<Example, std::string>> worked;
    for (std::size_t round = 0; round < 2; ++round)
        for (const ActKind kind : kShown) {
            const ActText& act = text_of(kind);
            if (offered(act, known, line, last_reply, frightened, worked_for) && !act.examples[round].line.empty())
                worked.emplace_back(act.examples[round], std::string(orchestrator::to_string(act.kind)));
        }
    Question q;
    q.messages = {{"system", "You sort what a traveller says to someone in a fantasy world into one act. Answer with the act's name "
                             "alone.\n\nThe acts:\n" +
                                 acts}};
    for (const auto& [example, name] : worked) {
        q.messages.push_back({"user", exchange(example.line, example.last_reply)});
        q.messages.push_back({"assistant", name});
    }
    q.messages.push_back({"user", exchange(line, last_reply)});
    q.grammar = "root ::= " + alternatives + "\n";
    return q;
}

Question Interpreter::target_question(ActKind act, std::string_view line, const Known& known, std::string_view last_reply) const {
    const ActText& text = text_of(act);
    if (!text.target)
        throw Error(std::string(orchestrator::to_string(act)) + (text.said ? "'s target is the line's own words" : " takes no target"));
    const std::string none(kNotListed);
    std::string listed;
    std::vector<std::string> labels;
    for (const Referent& r : candidates(*text.target, known, line, last_reply)) {
        listed += "- " + r.label + "\n";
        labels.push_back(r.label);
    }
    listed += "- " + none + "\n";
    labels.push_back(none);
    Question q;
    q.messages = {
        {"system", "A traveller in a fantasy world " + std::string(text.pointing) +
                       ". Answer with the one listed thing they mean, exactly as written: " + none + " if it is none of the others."},
        // Worked examples, from another place and time. Telling the model who is spoken to, so
        // that "you" could mean them, was measured three ways on 2026-09-24 and lowered
        // Qwen3-4B's score each time (77% of lines right, then 74% each). Shown for work, it
        // taught the model that only the speaker's own work counts; shown for people alone, it
        // took strangers for the speaker. "Who are you?" stays open (eval/README.md).
        {"user", "- Tamsin\n- Old Bram\n- " + none + "\n\nThe traveller says: \"Who is Brother Cadfael?\""},
        {"assistant", none},
        {"user", "- the mill of Oakmere\n- Oakmere\n- the old bridge\n- " + none + "\n\nThe traveller says: \"Tell me of the mill.\""},
        {"assistant", "the mill of Oakmere"},
        {"user", "- Old Bram\n- Tamsin\n- " + none + "\n\nThe traveller says: \"Tell me about Bram.\""},
        {"assistant", "Old Bram"},
        {"user", "- Bram: Mend the old bridge.\n- Tamsin: Clear the rats from the mill.\n- " + none +
                     "\n\nThey last said: \"Will you mend my bridge?\"\nThe traveller says: \"Yes, I'll do it.\""},
        {"assistant", "Bram: Mend the old bridge."},
        {"user", listed + "\n" + exchange(line, last_reply)},
    };
    q.grammar = "root ::= " + gbnf::one_of(labels) + "\n";
    return q;
}

Interpretation Interpreter::interpret(std::string_view line, const Known& known, std::string_view last_reply, bool frightened,
                                      bool worked_for) {
    text::check_line(line);
    text::check_line(last_reply);
    const auto ask = [&](const Question& q) {
        inference::Sampling sampling;
        sampling.max_tokens = 160;  // the grammar ends every answer; this only bounds a fault
        sampling.grammar = q.grammar;
        return model_.complete(model_.chat_prompt(q.messages), sampling).text;
    };
    // The grammar allows no other answer, so another is a fault in it or an answer cut short,
    // never the player's doing.
    const auto outside = [](const std::string& answer) { return Error("the model answered outside its grammar: " + answer); };

    const std::string act_answer = ask(act_question(line, known, last_reply, frightened, worked_for));
    const std::optional<ActKind> kind = orchestrator::act_kind(act_answer);
    if (!kind || !offered(text_of(*kind), known, line, last_reply, frightened, worked_for)) throw outside(act_answer);
    Interpretation out;
    out.act.kind = *kind;
    const ActText& act = text_of(*kind);
    // The name the player gives is their own words, as C++ found them (offered() found one).
    if (act.said) {
        out.act.target = out.label = *name_given(line, last_reply);
        return out;
    }
    if (!act.target) return out;

    // Someone asked of by the player's own words (contract version 25): a line that asks after a sovereign,
    // outright, by the name or the title it says; a name no packet gave, when the model finds none of those
    // told of. The director looks the words up.
    const std::optional<std::string> asked_of = *kind == ActKind::AskAboutPerson ? person_asked(line) : std::nullopt;
    const auto look_up = [&](const std::string& words) {
        out.act.target = "=" + words;
        out.label = words;
        return out;
    };
    if (asked_of && asks_after_a_sovereign(line)) return look_up(*asked_of);
    // A name said outright is the target; the model is asked only when none is.
    if (const std::optional<Referent> named = named_outright(*act.target, known, line)) {
        out.act.target = named->id;
        out.label = named->label;
    } else {
        const std::string label = known.nameable(*act.target, 1).empty() ? std::string(kNotListed)
                                                                          : ask(target_question(*kind, line, known, last_reply));
        if (label == kNotListed) {
            if (asked_of) return look_up(*asked_of);
            out.meant = *kind;
            out.act.kind = ActKind::NoAct;
            return out;
        }
        const std::optional<std::string> id = known.resolve(*act.target, label);
        if (!id) throw outside(label);
        out.act.target = *id;
        out.label = label;
    }
    out.confirm = confirm_question(out.act.kind);
    return out;
}

}  // namespace communicator
