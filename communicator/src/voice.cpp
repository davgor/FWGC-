#include "communicator/voice.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <set>

#include "communicator/error.hpp"
#include "communicator/known.hpp"
#include "gbnf.hpp"
#include "memory.hpp"
#include "news.hpp"
#include "reply_shape.hpp"
#include "text.hpp"

namespace communicator {

namespace {

using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

void check_version(const FactPacket& packet) {
    if (packet.contract_version != orchestrator::kContractVersion)
        throw Error("a packet of contract version " + std::to_string(packet.contract_version) + "; this reads version " +
                    std::to_string(orchestrator::kContractVersion));
}

std::string join(const std::vector<std::string>& parts, std::string_view between) {
    std::string out;
    for (const std::string& part : parts) out += (out.empty() ? "" : std::string(between)) + part;
    return out;
}

// A field's value as it would be said: names without their bracketed parts.
std::string value_of(const Fact& fact, std::string_view key) {
    const std::string* value = fact.field(key);
    return value ? text::spoken(*value) : std::string();
}

// A step on one of a packet's scales of 1 to 5, in words, with the number beside it: a number
// alone was misread ("danger level 5.1", difficulty 2 called hard; judged 2026-09-24).
std::string scaled(std::string_view key, const std::string& value) {
    const std::string word = text::scale_word(key, value);
    if (word.empty()) return std::string(key) + " " + value;
    if (key == "difficulty") return word + " work (" + value + " of 5)";
    return std::string(key) + " " + word + " (" + value + " of 5)";
}

// A fact's remaining fields as short phrases, in the director's order, skipping `used`.
std::vector<std::string> phrases(const Fact& fact, std::initializer_list<std::string_view> used) {
    std::vector<std::string> out;
    for (const auto& [key, raw] : fact.fields) {
        if (std::find(used.begin(), used.end(), key) != used.end()) continue;
        if (text::for_the_game(key)) continue;  // the game's map marker, never said
        const std::string value = text::spaced(text::spoken(raw));
        if (value.empty()) continue;
        if (key == "range") {  // roughly, never in metres
            if (const std::string words = text::range_words(raw); !words.empty()) out.push_back(words);
        } else if (key == "threat" || key == "danger" || key == "difficulty")  // each a scale of 1 to 5 in a packet
            out.push_back(scaled(key, value));
        else
            out.push_back(text::spaced(key) + ": " + value);
    }
    return out;
}

// One fact of `packet` as a line the speaker can draw on. Never its subject: an id is not something
// anyone says.
std::string fact_line(const Fact& fact, const FactPacket& packet) {
    switch (fact.kind) {
        case FactKind::Place:
            return "- " + text::spoken(label_of(fact)) + ": " + join(phrases(fact, {"name", "of"}), "; ");
        case FactKind::Person:
            return "- " + text::spoken(label_of(fact)) + ": " + join(phrases(fact, {"name"}), "; ");
        case FactKind::QuestOffer: {
            std::string line;
            if (value_of(fact, "from_speaker") == "yes") {
                line = "- Work you offer.";
            } else {
                const std::string giver = value_of(fact, "giver");
                const std::string role = text::spaced(value_of(fact, "giver_role"));
                line = "- Work " + (giver.empty() ? std::string("someone else") : giver) + (role.empty() ? "" : ", the " + role + ",") +
                       " offers.";
            }
            const std::string purpose = value_of(fact, "purpose");
            if (!purpose.empty()) line += " In the giver's words: \"" + purpose + "\"";
            // Not the days the offer stands: read as how long the work takes however it was put
            // ("948 days left", "to be done within", "the offer stands for"; judged 2026-09-24).
            // The game can show a deadline; the voice does not say it.
            const std::vector<std::string> rest = phrases(fact, {"giver", "giver_role", "from_speaker", "purpose", "days_left"});
            return line + (rest.empty() ? "" : " (" + join(rest, "; ") + ")");
        }
        case FactKind::QuestState:
            return "- The stranger's work is now " + value_of(fact, "state") + ".";
        case FactKind::Danger: {
            const std::string creature = value_of(fact, "creature");
            // A nest around the place asked about is as far from that place, not from the speaker.
            const bool around = value_of(fact, "around") == "yes";
            return std::string(around ? "- Danger near that place: " : "- Danger: ") +
                   (creature.empty() ? std::string("something") : creature) + " nesting: " +
                   join(phrases(fact, {"creature", "around"}), "; ") + (around ? ", from that place" : "");
        }
        case FactKind::Reason:
            return "- Why: " + value_of(fact, "reason");
        case FactKind::Chat: {
            std::vector<std::string> about;
            for (const std::string_view key : {"weather", "wind", "season", "time"})
                if (const std::string v = value_of(fact, key); !v.empty()) about.push_back(std::string(key) + ": " + v);
            return about.empty() ? std::string() : "- Here and now: " + join(about, "; ");
        }
        case FactKind::Safety: {
            const std::string verdict = value_of(fact, "verdict");
            if (verdict == "unsafe") return "- Your judgement: it is not safe here.";
            if (verdict == "wary") return "- Your judgement: safe enough, with care.";
            if (verdict == "safe") return "- Your judgement: it is safe here.";
            return {};
        }
        case FactKind::Reveal: {
            const std::string face = value_of(fact, "face");
            if (face == "unleashed") return "- The stranger has seen you unleashed, under the full moon.";
            if (face == "second") return "- The stranger has met your other self, the one that holds sway on other days.";
            const std::string was = text::spaced(value_of(fact, "was"));
            const std::string is = text::spaced(value_of(fact, "is"));
            if (is.empty() || was.empty() || is == was) return "- The stranger has seen through your mask.";
            return "- The stranger has seen through your mask: you are a " + is + ", not a " + was + ".";
        }
        case FactKind::Memory:
            return {};  // said apart, as what the speaker remembers (memory_lines)
        case FactKind::News: {
            // In C++'s words (news.hpp), never the fields for a model to make a story of; and only
            // news the written lines may pass on (may_pass_on): heard, not the speaker's own, and not
            // work an answer about a place lists already.
            if (!may_pass_on(fact, packet)) return {};
            const std::string words =
                news_words(fact, text::spoken(packet.persona.name), "the stranger", text::spoken(packet.persona.place));
            return "- News you have heard: " + words + " (word of it reached you).";
        }
    }
    return {};
}

// Whether the speaker passes on news the packet holds, as the written lines do (Lines::follow):
// what the stranger did, to a greeting; what happened, otherwise. Not a frightened or a cold speaker.
bool passes_on_news(const FactPacket& packet, bool of_the_stranger) {
    if (remembered(packet).frightened() || remembered(packet).cold()) return false;
    return news_to_pass_on(packet, of_the_stranger) != nullptr;
}

// What the speaker remembers of the stranger, as lines: nothing on a first meeting with nothing
// to remember.
std::vector<std::string> memory_lines(const FactPacket& packet) {
    const Remembered m = remembered(packet);
    std::vector<std::string> out;
    if (m.again) {
        const std::string when = since_words(m.since);
        out.push_back("You have met this stranger before" + (when.empty() ? std::string() : ", last " + when) + ".");
    } else {
        out.push_back("You have not met this stranger before.");
    }
    if (m.mood == "warm") out.push_back("You think well of them.");
    if (m.mood == "friendly") out.push_back("You like them well enough.");
    if (m.mood == "wary") out.push_back("You are wary of them.");
    if (m.mood == "hostile") out.push_back("You dislike them.");
    if (m.work == "offered") out.push_back("You have offered them work.");
    if (m.work == "taken") out.push_back("They took on your work, and it is not done yet.");
    if (m.work == "declined") out.push_back("They turned your work down.");
    if (m.work == "abandoned") out.push_back("They gave up the work they took from you.");
    if (m.work == "finished") out.push_back("They finished the work you gave them.");
    if (m.work == "failed") out.push_back("They failed the work you gave them.");
    if (!m.asked_before.empty()) out.push_back("Last time they asked you about " + text::spoken(m.asked_before) + ".");
    if (m.repeat) out.push_back("They have asked you this already: say so.");
    if (m.exposed) out.push_back("They know what you hide.");
    // How the speaker feels (contract version 9), which the game shows on their face too.
    if (m.feeling == "uneasy") out.push_back("You are uneasy.");
    if (m.feeling == "afraid") out.push_back("You are afraid, and it shows in how you speak.");
    if (m.feeling == "panicked") out.push_back("You are panicking: you speak in short, broken bursts.");
    // Talked down lately (contract version 10). Eased by this reply's calming words, it is what just
    // happened, not a memory: "lately" would claim an easing before it.
    if (m.eased && packet.outcome != Outcome::Calmed)
        out.push_back("The stranger talked you down from your fear lately; you are steadier for it.");
    return out;
}

// What the speaker is to do, given what the director made of the stranger's words.
std::string what_happened(const FactPacket& packet) {
    switch (packet.outcome) {
        case Outcome::Answered:
            switch (packet.answering) {
                case ActKind::AskSurroundings:
                    return "The stranger asked what is around. Tell them of a few of the places above, the nearest first, and "
                           "warn them of any danger.";
                case ActKind::AskAboutPlace:
                    // News a reply must show goes in what just happened (contract version 11).
                    return std::string("The stranger asked about the place above. Tell them what you know of it") +
                           (passes_on_news(packet, false) ? ", and the news of it above." : ".");
                case ActKind::AskAboutPerson:
                    if (!packet.facts.empty() && packet.facts.front().subject == packet.speaker)
                        return "The stranger asked who you are. Tell them.";
                    return std::string("The stranger asked about the person above. Tell them what you know of them") +
                           (passes_on_news(packet, false) ? ", and the news of them above." : ".");
                case ActKind::AskSafety:
                    return "The stranger asked whether it is safe here. Say plainly first whether it is, then name the worst "
                           "danger above.";
                case ActKind::AskForWork: {
                    if (packet.facts.empty()) return "The stranger asked for work. You know of none: say so.";
                    bool own = false;
                    for (const Fact& fact : packet.facts)
                        if (fact.kind == FactKind::QuestOffer && value_of(fact, "from_speaker") == "yes") own = true;
                    // "Yours first" only to a speaker with work of their own: told it with none, an
                    // innkeeper claimed another's work as hers (judged 2026-09-24).
                    return own ? "The stranger asked for work. Describe the work above in your own words: yours first, then "
                                 "who else is hiring."
                               : "The stranger asked for work. None of it is yours to give: say who is hiring, and for "
                                 "what.";
                }
                default:
                    return "Answer the stranger from what you know above.";
            }
        case Outcome::Accepted:
            return "The stranger has agreed to do the work they asked about. Thank them, and send them on their way.";
        case Outcome::Declined:
            return "The stranger turned the work down. Take it in your stride.";
        case Outcome::Abandoned:
            return "The stranger has given up the work they took on. Say how you take it.";
        case Outcome::Unknown:
            return "You know nothing of what the stranger asked about. Say so, in your own way.";
        case Outcome::Refused: {
            std::string why;
            for (const Fact& fact : packet.facts)
                if (fact.kind == FactKind::Reason) why = value_of(fact, "reason");
            return "You refuse what the stranger asked" + (why.empty() ? std::string() : ", because " + why) +
                   ". Say no, in your own way.";
        }
        case Outcome::Parted:
            return "The stranger is leaving. Say goodbye.";
        case Outcome::NotUnderstood:
            return "You do not follow what the stranger wants, or it is nothing you can help with. Say so in a sentence, and "
                   "tell them nothing else.";
        case Outcome::Greeted:
            if (passes_on_news(packet, true))
                return "The stranger greeted you, and you have heard what they did (the news above). Greet them back, and say "
                       "you heard of it.";
            return "The stranger greeted you. Greet them back, as you take them.";
        case Outcome::Chatted:
            // "Answer in kind" won over "You dislike them" for every model tried (2026-09-26).
            if (remembered(packet).cold() && !remembered(packet).frightened())
                return std::string("The stranger is making small talk, and you ") +
                       (remembered(packet).mood == "hostile" ? "dislike them" : "are wary of them") +
                       ". You want no chat with them: answer curtly, and give them nothing.";
            if (passes_on_news(packet, false))
                return "The stranger is making small talk. Answer in kind, and pass on the news above.";
            return "The stranger is making small talk. Answer in kind, from the weather and the season above.";
        case Outcome::Acknowledged:
            return "The stranger answered what you asked. Take it in, in your own way.";
        case Outcome::Thanked:
            return "The stranger thanked you. Answer it, as you take them.";
        case Outcome::Offended:
            return "The stranger insulted you. Show how you take it.";
        case Outcome::Exposed:
            return "The stranger has seen through you, and you own to it. Say so, as who you truly are.";
        case Outcome::Denied:
            return "The stranger says you are not what you seem. Deny it.";
        case Outcome::Forgiven:
            return "The stranger said sorry for a wrong they did you, and you forgive it. Say so, as you take them now.";
        case Outcome::Unwronged:
            return "The stranger said sorry, but they have done you no wrong. Say so.";
        case Outcome::Calmed:
            return "The stranger's words ease your fear a little. Say so, as frightened as you still are.";
        case Outcome::StillAfraid:
            return "The stranger tried to calm you, and you are still afraid. Say so.";
        case Outcome::NotAfraid:
            // "You are not afraid. Say so." won over the memory of being talked down, for every
            // model tried (2026-09-26).
            if (remembered(packet).eased)
                return "The stranger tried to calm you again, but they talked you down lately and you are all right now. "
                       "Thank them for it, and say you are steady.";
            return "The stranger tried to calm you, but you are not afraid. Say so.";
    }
    return {};
}

// A code point's value, from the UTF-8 character at `text[i]`; `width` is set to its length.
char32_t code_point(std::string_view text, std::size_t i, std::size_t& width) {
    const auto lead = static_cast<unsigned char>(text[i]);
    width = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
    if (i + width > text.size()) width = 1;
    if (width == 1) return lead;
    char32_t value = lead & (0xFF >> (width + 1));
    for (std::size_t k = 1; k < width; ++k) value = (value << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
    return value;
}

// Whether a code point is part of a word: a letter, as far as a name can need one. Punctuation
// outside ASCII (dashes, curly quotes, the ellipsis) is not.
bool in_word(char32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (c < 0xC0 || c == 0xD7 || c == 0xF7) return false;
    return !(c >= 0x2000 && c <= 0x206F) && !(c >= 0x3000 && c <= 0x303F);
}

// The words of `text` that a reply could not write in lower case: those that begin with a
// capital, or with a letter outside ASCII.
void capitals_of(std::string_view text, std::set<std::string>& out) {
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t width = 1;
        if (!in_word(code_point(text, i, width))) {
            i += width;
            continue;
        }
        const std::size_t start = i;
        while (i < text.size() && in_word(code_point(text, i, width))) i += width;
        const char first = text[start];
        if (!(first >= 'a' && first <= 'z')) out.emplace(text.substr(start, i - start));
    }
}

// The words of `text` in lower case: runs of ASCII letters, lowered.
void lower_words_of(std::string_view text, std::set<std::string>& out) {
    std::string word;
    for (const char c : text) {
        if (c >= 'A' && c <= 'Z') {
            word += static_cast<char>(c - 'A' + 'a');
        } else if (c >= 'a' && c <= 'z') {
            word += c;
        } else if (!word.empty()) {
            out.insert(word);
            word.clear();
        }
    }
    if (!word.empty()) out.insert(word);
}

}  // namespace

const std::vector<std::string>& common_capitals() {
    static const std::vector<std::string> words = {
        // Pronouns and determiners.
        "I", "You", "He", "She", "It", "We", "They", "Me", "My", "Your", "Yours", "His", "Her", "Hers", "Its", "Our", "Ours",
        "Their", "Theirs", "Us", "Them", "Mine", "Myself", "Yourself", "The", "A", "An", "This", "That", "These", "Those",
        "Some", "Any", "No", "Every", "Each", "All", "Both", "Few", "Many", "Much", "Most", "More", "Less", "Other",
        "Another", "Such", "Several", "Neither", "Either", "Enough", "One", "Two", "Three", "Four", "Five", "Half",
        // Joining words.
        "And", "But", "Or", "So", "Yet", "For", "Nor", "If", "Then", "When", "While", "As", "Because", "Though",
        "Although", "Unless", "Until", "Since", "Once", "Whether", "At", "In", "On", "Of", "To", "From", "With",
        "Without", "By", "About", "Above", "Below", "Over", "Under", "Up", "Down", "Out", "Off", "Near", "Past", "Beyond",
        "Through", "Across", "Along", "Around", "Into", "Upon", "Within", "Between", "Among", "Toward", "Towards",
        "Behind", "Beside", "Before", "After", "Against", "During", "Inside", "Outside",
        // Questions.
        "What", "Who", "Whom", "Whose", "Which", "Where", "Why", "How", "Whatever", "Whoever", "Wherever",
        // Verbs that open a question or an order, and the stems of their contractions (Don't).
        "Is", "Are", "Was", "Were", "Be", "Do", "Does", "Did", "Have", "Has", "Had", "Can", "Could", "Will", "Would",
        "Shall", "Should", "May", "Might", "Must", "Let", "Go", "Come", "Take", "Look", "Listen", "Hear", "Heed", "Mind",
        "Keep", "Stay", "Leave", "Tell", "Ask", "Find", "Follow", "Bring", "Seek", "Watch", "Beware", "Wait", "Speak",
        "See", "Make", "Give", "Get", "Know", "Trust", "Remember", "Forgive", "Pardon", "Say", "Help", "Hold", "Head",
        "Turn", "Rest", "Don", "Doesn", "Didn", "Isn", "Aren", "Wasn", "Weren", "Haven", "Hasn", "Hadn", "Won", "Wouldn",
        "Couldn", "Shouldn", "Mustn",
        // Answers, greetings and turns of speech.
        "Yes", "Aye", "Nay", "Not", "Ah", "Oh", "Ha", "Hm", "Hmm", "Well", "Now", "Here", "There", "Never", "Always",
        "Often", "Sometimes", "Perhaps", "Maybe", "Indeed", "Truly", "Surely", "Sadly", "Alas", "Please", "Thank",
        "Thanks", "Greetings", "Hello", "Hail", "Farewell", "Goodbye", "Welcome", "Good", "Safe", "Just", "Only", "Even",
        "Still", "Also", "Too", "Again", "Soon", "Later", "Today", "Tonight", "Tomorrow", "First", "Last", "Next", "Very",
        "Quite", "Rather", "Certainly", "Sure", "Right", "Fine", "True", "Nothing", "Something", "Anything", "Everything",
        "Nobody", "Somebody", "Anyone", "Someone", "Everyone", "None", "Instead", "Otherwise", "Besides",
        // No compass point: a packet holds distances, never bearings, and a reply that gives one
        // invents it.
    };
    return words;
}

const std::vector<std::string>& forbidden_words() {
    static const std::vector<std::string> words = {
        // Compass points, as stems. With only whole words barred, Qwen3-4B ran on past them 9
        // times in four runs of 24 replies: "northwardstretch", "westwardwhere", "westernmost"
        // (2026-09-24).
        "north*", "south*", "east*", "west*",
        // Money, payment and reward. Only "pay", "repay" and "reward" are stems: every word that
        // begins with one of them is about pay, and "golden" and "coincidence" are not. Nor is
        // "crown" barred: in a fantasy world it is as often the throne.
        "pay*", "repay*", "reward*", "paid", "repaid", "gold", "silver", "copper", "coin", "coins", "coinage", "penny",
        "pennies", "pence", "shilling", "shillings", "money", "cash", "wage", "wages", "salary", "stipend", "bounty",
        "bounties", "fee", "fees", "price", "prices", "purse", "purses", "recompense", "compensation", "treasure",
        "treasures", "riches", "loot",
    };
    return words;
}

const std::vector<std::string>& counting_words() {
    // Stems wherever no word worth saying begins with one: barred whole, "days" came back glued,
    // "three daysago the high road" (a mutation pass, 2026-09-26), as "north" had come back as
    // "northwardstretch". "ten" stays whole, for "tend" and "tender"; "pace" and the singular
    // times stay sayable ("a steady pace", "good day", "an hour's walk").
    static const std::vector<std::string> words = {
        // Numbers from two up: "one" stays, for "no one" and "one day". A stem bars its teens and
        // tens too ("four*": "fourteen"), and its ordinal, which counts as well ("the fourth day").
        "two*", "three*", "four*", "five*", "six*", "seven*", "eight*", "nine*", "ten", "eleven*", "twelve*", "thirteen*",
        "fifteen*", "twenty*", "thirty*", "forty*", "fifty*", "hundred*", "thousand*", "dozen*", "fortnight*",
        // Units of distance. A packet says how far in words, never in these.
        "mile*", "league*", "furlong*", "kilometre*", "kilometer*", "km", "paces*", "strides*",
        // Times in the plural.
        "minutes*", "hours*", "days*", "nights*", "weeks*", "months*", "years*",
    };
    return words;
}

Voice::Voice(inference::Model& model, VoiceOptions options) : model_(model), options_(options) {
    if (options_.max_tokens < 1) throw Error("max_tokens must be at least 1");
    if (!std::isfinite(options_.temperature) || options_.temperature < 0.0f)
        throw Error("temperature must be a finite number, zero or more");
}

std::vector<inference::Message> Voice::messages(const FactPacket& packet, std::string_view line) {
    check_version(packet);
    const orchestrator::Persona& p = packet.persona;
    const std::string name = text::spoken(p.name), role = text::spaced(text::spoken(p.role)), place = text::spoken(p.place);
    std::string system = "You are " + (name.empty() ? std::string("someone") : name) + (role.empty() ? "" : ", " + role) +
                         (place.empty() ? "" : ", at " + place) +
                         ", a person in a fantasy world. A stranger has come up to you and speaks to you.\n";
    // The card's sample line is left out. Shown to the model in any framing, it was said back
    // whole: in 9 of 24 replies from Qwen3-4B, "never to be repeated word for word" or not
    // (judged 2026-09-24). So are its manner and its voice line, whose words came back as facts
    // however they were framed: the watch rota, oaths sworn ("precedent and oaths cited when
    // pressed"). The card's stance and never-lines carry the character instead.
    std::vector<std::string> card;
    if (!p.toward_player.empty()) card.push_back("Toward strangers: " + text::one_line(p.toward_player));
    if (!p.never.empty()) {
        std::vector<std::string> never;
        for (const std::string& n : p.never) never.push_back(text::one_line(n));
        card.push_back("You never: " + join(never, "; ") + ".");
    }
    if (!card.empty()) {
        system += "Your character, which shapes how you speak and is never something to tell:\n";
        for (const std::string& trait : card) system += "- " + trait + "\n";
    }
    system += "\nWhat you know and may tell:\n";
    std::string known;
    for (const Fact& fact : packet.facts)
        if (const std::string said = fact_line(fact, packet); !said.empty()) known += said + "\n";
    system += known.empty() ? "- nothing that bears on this\n" : known;
    system += "\nWhat you remember of the stranger:\n";
    for (const std::string& remembered_line : memory_lines(packet)) system += "- " + remembered_line + "\n";
    // A bystander who joined unasked (contract version 10) says so first: told it only as something
    // they remembered, Qwen3-4B answered straight away (2026-09-26).
    const std::string joined = remembered(packet).joined
                                   ? "You overheard the stranger ask someone else, and came over unasked. Say first that you "
                                     "overheard, then answer. "
                                   : "";
    system += "\nWhat just happened: " + joined + what_happened(packet) + "\n\n";
    const int sentences = reply_shape(packet).sentences;
    const std::string length =
        sentences == 1 ? "in one short sentence" : "in one to " + std::string(sentences == 2 ? "two" : "three") + " short sentences";
    system += "Reply as " + (name.empty() ? std::string("yourself") : name) + ", " + length +
              " of spoken words. Speak of no person or place that is not named above, and say nothing of pay. Say how far "
              "the places above are only in the words given, never which way: you know no directions.";
    return {{"system", system}, {"user", text::one_line(line)}};
}

std::string Voice::grammar(const FactPacket& packet) {
    check_version(packet);
    // Every capitalised word the prompt gives as something to say may be said anywhere: the
    // speaker's name, post and place, and the facts' words; and "I". The never-lines and the
    // card's descriptions are not the speaker's words, and the card's sample line is not shown,
    // so they add none. The other common capitals may only begin a sentence: mid-sentence,
    // Qwen3-4B made names of them ("a castle named Greetings", "the Watch Right"; judged
    // 2026-09-24).
    std::set<std::string> capitals = {"I"};
    std::set<std::string> packet_words;
    for (const std::string* said : {&packet.persona.name, &packet.persona.role, &packet.persona.place}) {
        capitals_of(*said, capitals);
        lower_words_of(*said, packet_words);
    }
    for (const Fact& fact : packet.facts)
        for (const auto& [key, value] : fact.fields) {
            // A marker is the game's, never said: its id's words are no words the speaker has.
            if (text::for_the_game(key)) continue;
            capitals_of(value, capitals);
            lower_words_of(value, packet_words);
        }
    std::vector<std::string> common;
    for (const std::string& word : common_capitals())
        if (!capitals.count(word)) common.push_back(word);

    // The reply's shape: its opening words, then whole sentences up to the shape's count, then
    // the closing C++ wrote, word for word.
    const ReplyShape shape = reply_shape(packet);
    const std::vector<std::string>& openings = shape.openings.empty() ? shape.stance : shape.openings;
    std::string root = openings.empty() ? "sentence" : "first";
    std::string more;
    for (int i = 1; i < shape.sentences; ++i) more = "(\" \" sentence" + (more.empty() ? "" : " " + more) + ")?";
    if (!more.empty()) root += " " + more;
    if (!shape.closing.empty()) root += " " + gbnf::literal(" " + shape.closing);

    // A sentence is words parted by gaps, then an end. Its first word may be a common capital.
    // Any word is all lower case, a number, or one of the packet's capitalised words whole: a
    // capital word followed straight on by letters ("Theodric" as "The" + "odric") is not a word
    // here. A lower-case word is not forbidden, unless the packet's own text says it: a place may
    // be the silver mine. Nor is a word that counts out a distance or a time (counting_words()),
    // unless the packet's own text says it. The fixed openings go on with words, so a common
    // capital follows none.
    std::vector<std::string> barred = forbidden_words();
    barred.insert(barred.end(), counting_words().begin(), counting_words().end());
    std::string g = "root ::= " + root + "\n";
    if (!openings.empty()) g += "first ::= " + gbnf::one_of(openings) + " (gap token)* gap? end\n";
    if (!common.empty()) g += "common ::= " + gbnf::one_of(common) + "\n";
    g += "sentence ::= gap? opener (gap token)* gap? end\n"
         "opener ::= token" +
         std::string(common.empty() ? "" : " | common") +
         "\n"
         "token ::= lower | capital | number\n"
         "lower ::= " +
         gbnf::word_except(barred, std::vector<std::string>(packet_words.begin(), packet_words.end()),
                           "\\u00DF-\\u00F6\\u00F8-\\u00FF", "rest") +
         "\n"
         "rest ::= [a-z\\u00DF-\\u00F6\\u00F8-\\u00FF]*\n"
         "capital ::= " +
         gbnf::one_of(std::vector<std::string>(capitals.begin(), capitals.end())) +
         "\n"
         "number ::= [0-9]+ (\".\" [0-9]+)?\n"
         // No double quotes: after a stop, one was, 14 times in 24, a quote never opened
         // (judged 2026-09-24), and spoken words shown in a dialogue box need none.
         // Apostrophes stay, for contractions. A sentence ends with one stop, an ellipsis, or
         // ?! and !?: any run of stops let a reply spin out "..?..?" to its token limit.
         "gap ::= [ ,;:'()\\u2013\\u2014\\u2018\\u2019-]+\n"
         "end ::= \"...\" | \"?!\" | \"!?\" | [.!?]\n";
    return g;
}

Reply Voice::speak(const FactPacket& packet, std::string_view line, const std::function<bool(std::string_view)>& on_piece) {
    text::check_line(line);
    inference::Sampling sampling;
    sampling.max_tokens = options_.max_tokens;
    sampling.temperature = options_.temperature;
    sampling.seed = options_.seed;
    sampling.grammar = grammar(packet);
    const inference::Completion c = model_.complete(model_.chat_prompt(messages(packet, line)), sampling, on_piece);
    return Reply{c.text, c.tokens, c.stopped_at_end, c.stopped_by_caller};
}

}  // namespace communicator
