// News a speaker has heard (contract version 11): the words C++ says it in, the written line said
// after a reply, the voice told to pass it on, and the truth check holding news to a News fact. No
// model.
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "communicator/authored.hpp"
#include "communicator/truth.hpp"
#include "communicator/voice.hpp"
#include "news.hpp"
#include "packets.hpp"

namespace {

using communicator::Lines;
using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

Fact exposed(const std::string& player) {
    return {FactKind::News,
            "event-40",
            {{"event", "exposed"},
             {"state", "exposed"},
             {"place", "Wenweg"},
             {"person", "Maren"},
             {"was", "herbalist"},
             {"is", "cult_leader"},
             {"player", player},
             {"since", "yesterday"},
             {"how", "word"}}};
}

const Lines& news_lines() {
    static const Lines lines = Lines::parse("any | greeted | Good day.\n"
                                            "any | greeted_cold | What now.\n"
                                            "any | greeted_afraid | Oh! You startled me.\n"
                                            "any | small_talk | Fine weather.\n"
                                            "any | about_place | {about}.\n"
                                            "any | heard_deed | You're the one who {deed}, aren't you?\n"
                                            "any | news | The word is {news}.\n",
                                            "news");
    return lines;
}

bool flags_news(const std::string& reply, const FactPacket& p) {
    for (const std::string& problem : communicator::untrue(reply, p, {}))
        if (problem.rfind("news the speaker has not heard:", 0) == 0) return true;
    return false;
}

}  // namespace

TEST(news_is_said_in_words_the_news_fact_holds) {
    using communicator::deed_words;
    using communicator::news_words;
    // What the stranger did, by the work's verb and the place it was for.
    check::equal(deed_words(packets::news_of_work("finished", true), ""), std::string("helped Wenweg hold"), "defended");
    check::equal(news_words(packets::news_of_work("finished", true), ""), std::string("you helped Wenweg hold"), "said to them");
    check::equal(news_words(packets::news_of_work("finished", true), "", "the stranger"), std::string("the stranger helped Wenweg hold"),
                 "said of them");
    check::equal(deed_words(packets::news_of_work("taken", true), ""), std::string("took on Halbrann's work"), "taken");
    // Not the stranger's doing: no deed, and the work as its giver asked it.
    check::equal(deed_words(packets::news_of_work("asked", false), ""), std::string(), "asked is nobody's deed");
    check::equal(news_words(packets::news_of_work("asked", false), ""),
                 std::string("Halbrann wants someone to stand with Wenweg when they come for it"), "asked, by what Halbrann asked");
    Fact unstated = packets::news_of_work("asked", false);
    unstated.fields.erase(std::find_if(unstated.fields.begin(), unstated.fields.end(), [](const auto& f) { return f.first == "purpose"; }));
    check::equal(news_words(unstated, ""), std::string("Halbrann is looking for help with Wenweg"), "asked, with no purpose stated");
    check::equal(news_words(unstated, "", "you", "Wenweg"), std::string("Halbrann is looking for help here"), "asked, here");
    check::equal(news_words(packets::news_of_work("lapsed", false), ""), std::string("nobody answered Halbrann's call for help"), "lapsed");
    // A scare, by who ended it.
    check::equal(deed_words(packets::news_of_a_scare("beaten", "player"), ""), std::string("beat off the threat at the fortress of Wenweg"),
                 "beaten by the stranger");
    check::equal(news_words(packets::news_of_a_scare("beaten", "Orlan"), ""), std::string("Orlan beat off the threat at the fortress of Wenweg"),
                 "beaten by someone else");
    check::equal(news_words(packets::news_of_a_scare("beaten", ""), ""), std::string("the threat at the fortress of Wenweg was beaten off"),
                 "beaten, by nobody said");
    check::equal(news_words(packets::news_of_a_scare("frightened", ""), ""), std::string("there was a scare at the fortress of Wenweg"),
                 "frightened");
    // A hidden face, by the roles it claimed and hid.
    check::equal(deed_words(exposed("yes"), ""), std::string("showed that Maren is a cult leader"), "unmasked by the stranger");
    check::equal(news_words(exposed("no"), ""), std::string("Maren, who passed for a herbalist, is a cult leader"), "unmasked");
    // Nothing of the speaker's own work, of themself, or of what they did: their own lines say that.
    check::equal(news_words(packets::news_of_work("finished", true), "Halbrann"), std::string(), "not to the giver");
    check::equal(news_words(exposed("yes"), "Maren"), std::string(), "not to the one unmasked");
    check::equal(news_words(packets::news_of_a_scare("beaten", "Orlan"), "Orlan"), std::string(), "not to the one who beat it off");
    // A place as someone standing there names it (judged 2026-09-27: "the fortress of Iskmor" from
    // the people of Iskmor read as a label off a map): named for where they stand, by its kind; the
    // place itself, here; and a defence of it, theirs.
    check::equal(deed_words(packets::news_of_a_scare("beaten", "player"), "", "Wenweg"), std::string("beat off the threat at the fortress"),
                 "the fortress of the town the speaker stands in");
    check::equal(deed_words(packets::news_of_a_scare("beaten", "player"), "", "the fortress of Wenweg"),
                 std::string("beat off the threat here"), "where the speaker stands");
    check::equal(news_words(packets::news_of_a_scare("frightened", ""), "", "you", "the fortress of Wenweg"),
                 std::string("there was a scare here"), "a scare where the speaker stands");
    check::equal(deed_words(packets::news_of_work("finished", true), "", "Wenweg"), std::string("helped us hold"), "a defence of their own town");
    check::equal(deed_words(packets::news_of_work("finished", true), "", "Torhas"), std::string("helped Wenweg hold"), "and of another");
    check::equal(deed_words(packets::news_of_a_scare("beaten", "player"), "", "Linbrig"),
                 std::string("beat off the threat at the fortress of Wenweg"), "elsewhere, by its whole name");
}

TEST(news_is_dated_and_old_news_comes_up_less) {
    using communicator::brought_up;
    using communicator::dated_deed_words;
    using communicator::dated_news_words;
    const auto aged = [](Fact f, const std::string& since) {
        for (auto& [key, value] : f.fields)
            if (key == "since") value = since;
        return f;
    };
    // "Lets date the news" (the owner, 2026-09-27): a deed, a scare and a lapsed call by when they
    // came about; what still holds, not.
    check::equal(dated_deed_words(aged(packets::news_of_work("finished", true), "days"), "", "Wenweg"), std::string("helped us hold a few days back"),
                 "a deed, some days on");
    check::equal(dated_news_words(packets::news_of_a_scare("beaten", "Orlan"), "", "you", "Wenweg"),
                 std::string("Orlan beat off the threat at the fortress today"), "a scare ended today");
    check::equal(dated_news_words(aged(packets::news_of_work("lapsed", false), "months"), ""),
                 std::string("nobody answered Halbrann's call for help months back"), "a call nobody answered");
    check::equal(dated_news_words(aged(packets::news_of_work("asked", false), "weeks"), ""),
                 std::string("Halbrann wants someone to stand with Wenweg when they come for it"), "work still asked for: not dated");
    check::equal(dated_news_words(exposed("no"), ""), std::string("Maren, who passed for a herbalist, is a cult leader"), "who someone is: not dated");
    // "Relevant news is the talk of the town, old news is less likely to pop up": brought up unasked
    // by every speaker while fresh, by fewer as it ages, and by none past two months.
    const auto speakers_bringing_up = [&](const std::string& since, bool about) {
        int count = 0;
        for (int i = 0; i < 200; ++i) {
            FactPacket p = about ? packets::about_the_town() : packets::small_talk("clear", "breezy");
            p.speaker = "npc-roster-" + std::to_string(i);
            if (brought_up(aged(packets::news_of_work("finished", true), since), p)) ++count;
        }
        return count;
    };
    check::equal(speakers_bringing_up("today", false), 200, "today's: every speaker");
    check::equal(speakers_bringing_up("yesterday", false), 200, "yesterday's: every speaker");
    const int days = speakers_bringing_up("days", false), weeks = speakers_bringing_up("weeks", false);
    check::is_true(days > 70 && days < 130, "this week's: about one speaker in two, " + std::to_string(days) + " of 200");
    check::is_true(weeks > 15 && weeks < 65, "these two months': about one in five, " + std::to_string(weeks) + " of 200");
    check::equal(speakers_bringing_up("months", false), 0, "older: none unasked");
    check::equal(speakers_bringing_up("years", false), 0, "much older: none unasked");
    check::is_true(speakers_bringing_up("months", true) > 15, "asked about its place, a band further back");
    // Unasked, old news waits; asked for, it is told, dated.
    const Lines lines = Lines::parse("any | small_talk | Fine weather.\n"
                                     "any | news | The word is {news}.\n",
                                     "news");
    FactPacket old = packets::small_talk("clear", "breezy");
    old.facts.insert(old.facts.end() - 1, aged(packets::news_of_a_scare("beaten", "Orlan"), "months"));
    check::equal(lines.follow(old), std::string(), "old news is not brought up after small talk");
    const std::vector<std::string> asked = lines.news_asked(old);
    check::is_true(asked.size() == 1 && asked.front() == "The word is Orlan beat off the threat at the fortress months back.",
                   "asked for news, the old news, dated: " + (asked.empty() ? std::string("none") : asked.front()));
    // The truth check reads a News fact's dated words as the news's date, not as when the speaker
    // last met the stranger: a first meeting may say "you helped us hold yesterday".
    FactPacket first = packets::greeted_first();
    first.facts.insert(first.facts.end() - 1, packets::news_of_work("finished", true));
    check::is_true(communicator::untrue("You're the one who helped us hold yesterday, aren't you?", first, {}).empty(),
                   "the news's date, backed by the News fact");
    check::is_true(!communicator::untrue("I saw you yesterday.", first, {}).empty(), "the control: a meeting yesterday, on a first meeting");
}

TEST(only_news_heard_is_passed_on_and_what_happened_to_the_speaker_is_not) {
    using communicator::news_to_pass_on;
    FactPacket chat = packets::small_talk("clear", "breezy");
    chat.facts.insert(chat.facts.end() - 1, packets::news_of_a_scare("frightened", ""));
    check::is_true(news_to_pass_on(chat, false) != nullptr, "the premise: a scare heard of is passed on");
    // Frightened there, or talked down there: theirs to say in their own lines, not word going round
    // (writers found hearsay of what the speaker saw, 2026-09-27).
    for (Fact& fact : chat.facts)
        for (auto& [key, value] : fact.fields)
            if (key == "how") value = "there";
    check::is_true(news_to_pass_on(chat, false) == nullptr, "not what happened to them");
    // A hero's own deed is not news they heard.
    FactPacket orlan = packets::about_the_town_with_news();
    check::is_true(news_to_pass_on(orlan, false) != nullptr, "the premise: Orlan's deed, told of by another");
    orlan.persona.name = "Orlan";
    check::is_true(news_to_pass_on(orlan, false) == nullptr, "not by Orlan");
}

TEST(news_is_said_after_the_reply_once_and_never_by_the_cold_or_the_frightened) {
    const Lines& lines = news_lines();
    check::equal(lines.speak(packets::greeted_having_heard(), "Hello."),
                 std::string("Good day. You're the one who helped Wenweg hold yesterday, aren't you?"),
                 "a greeting, then what they heard of the stranger, dated");
    // What the stranger did is said to them as their deed after small talk too, never handed to them
    // as news (the second judge, 2026-09-27).
    check::equal(lines.speak(packets::small_talk_with_news(), "Fine day."),
                 std::string("Fine weather. You're the one who beat off the threat at the fortress today, aren't you?"),
                 "small talk, then what the stranger did");
    // After an answer about a place, its news, but not the work asked for there, which the answer
    // lists already; small talk, which lists nothing, passes that on.
    const std::string about = lines.speak(packets::about_the_town_with_news(), "Tell me about Wenweg.");
    const std::string then = " The word is Orlan beat off the threat at the fortress today.";
    check::is_true(about.size() > then.size() && about.compare(about.size() - then.size(), then.size(), then) == 0,
                   "an answer about a place, then its news: " + about);
    FactPacket only_asked = packets::about_the_town();
    only_asked.facts.insert(only_asked.facts.end() - 1, packets::news_of_work("asked", false));
    check::equal(lines.follow(only_asked), std::string(), "not the work asked for, after an answer that lists it");
    const FactPacket chat_asked = packets::small_talk_with_word_of("asked", false);
    check::equal(lines.follow(chat_asked), std::string("The word is Halbrann wants someone to stand with Wenweg when they come for it."),
                 "but small talk passes it on");
    // Work its giver's offer named lately is news told: an answer listing "Halbrann wants someone to
    // stand with Wenweg" is not followed by the same work as news.
    check::equal(lines.follow(chat_asked, {"Halbrann the pretender wants someone to stand with Wenweg when they come for it."}),
                 std::string(), "not the work an offer named lately");
    // The controls: no news, no sentence after.
    check::equal(lines.speak(packets::greeted_first(), "Hello."), std::string("Good day."), "no news, nothing more");
    // Told lately, not told again, however it was put: said in a greeting as what the stranger did,
    // it is not said again in small talk as news (the first news session, 2026-09-27).
    const std::string greeted = "Good day. You're the one who helped Wenweg hold yesterday, aren't you?";
    check::equal(lines.speak(packets::greeted_having_heard(), "Hello.", {greeted}), std::string("Good day."),
                 "news said lately is not said again");
    FactPacket welcome = packets::greeted_first();  // the speaker of the small talk below, at Wenweg
    welcome.facts.insert(welcome.facts.end() - 1, packets::news_of_work("finished", true));
    const std::string welcomed = lines.speak(welcome, "Hello.");
    check::equal(welcomed, std::string("Good day. You're the one who helped us hold yesterday, aren't you?"), "the premise: a greeting at Wenweg");
    const FactPacket chat = packets::small_talk_with_word_of("finished", true);
    check::equal(lines.speak(chat, "Fine day."), std::string("Fine weather. You're the one who helped us hold yesterday, aren't you?"),
                 "the premise: small talk passes it on");
    check::equal(lines.speak(chat, "Fine day.", {welcomed}), std::string("Fine weather."), "not after the greeting said it");
    // A cold speaker, and a frightened one, say their own lines alone.
    FactPacket cold = packets::greeted_having_heard();
    cold.facts.back() = packets::memory_of(cold.speaker, {.mood = "hostile"});
    check::equal(lines.speak(cold, "Hello."), std::string("What now."), "not from a cold speaker");
    FactPacket afraid = packets::greeted_having_heard();
    afraid.facts.back() = packets::memory_of(afraid.speaker, {.feeling = "afraid"});
    check::equal(lines.speak(afraid, "Hello."), std::string("Oh! You startled me."), "nor from a frightened one");
    // The giver of the work does not tell the stranger what they did for the giver.
    FactPacket giver = packets::greeted_having_heard();
    giver.persona.name = "Halbrann";
    check::equal(lines.speak(giver, "Hello."), std::string("Good day."), "not their own work");
}

TEST(news_waits_for_the_answer_to_a_question) {
    // "Here for a fitting, or just talk? So you're the one who helped the fortress of Iskmor hold."
    // (judged 2026-09-27): with news to tell, a line that asks is not said while one that does not
    // fits; with only lines that ask, the news waits.
    const Lines lines = Lines::parse("any | greeted | Here for a fitting, or just talk?\n"
                                     "any | greeted | Good day.\n"
                                     "any | heard_deed | So you're the one who {deed}.\n",
                                     "news");
    const auto greeting = [](const FactPacket& base, int heard) {
        FactPacket p = base;
        p.facts.back() = packets::memory_of(p.speaker, {.heard = heard});
        return p;
    };
    std::set<std::string> plain;
    for (int heard = 0; heard < 4; ++heard) plain.insert(lines.speak(greeting(packets::greeted_first(), heard), "Hello."));
    check::equal(plain.size(), std::size_t(2), "the premise: without news, both greetings in turn");
    for (int heard = 0; heard < 4; ++heard)
        check::equal(lines.speak(greeting(packets::greeted_having_heard(), heard), "Hello."),
                     std::string("Good day. So you're the one who helped Wenweg hold yesterday."), "with news, the greeting that does not ask");
    const Lines asking = Lines::parse("any | greeted | Here for a fitting, or just talk?\n"
                                      "any | heard_deed | So you're the one who {deed}.\n",
                                      "news");
    check::equal(asking.speak(packets::greeted_having_heard(), "Hello."), std::string("Here for a fitting, or just talk?"),
                 "only a question to ask: the news waits");
}

TEST(asked_for_news_the_speaker_gives_it_or_says_there_is_no_more) {
    const Lines lines = Lines::parse("any | small_talk | Fine weather.\n"
                                     "any | small_talk_cold | Not now.\n"
                                     "any | heard_deed | They say you {deed}.\n"
                                     "any | heard_deed_told | Only the talk of what you did, and you'd tell that better than me.\n"
                                     "any | news | The word is {news}.\n"
                                     "any | news_told | As I said, {news}.\n"
                                     "any | no_news | Nothing's reached me.\n",
                                     "news");
    const auto all = [](const std::vector<std::string>& said) {
        std::string out;
        for (const std::string& s : said) out += (out.empty() ? "" : " | ") + s;
        return out;
    };
    // Of others: the news, then, told lately, no more than was told.
    const FactPacket others = packets::small_talk_with_word_of("asked", false);
    const std::string work = "The word is Halbrann wants someone to stand with Wenweg when they come for it.";
    check::equal(all(lines.news_asked(others)), work, "news of others not told yet, said alone");
    check::equal(all(lines.news_asked(others, {work})), std::string("As I said, Halbrann wants someone to stand with Wenweg when they come for it."),
                 "told lately: no more than was told");
    // Of the stranger: their deed, then, told lately, the talk of it, never read back as fact: they
    // never said it was them ("As I said, you helped the fortress hold." was the second judge's worst
    // line, 2026-09-27).
    const FactPacket chat = packets::small_talk_with_news();
    check::equal(all(lines.news_asked(chat)), std::string("They say you beat off the threat at the fortress today."), "the stranger's deed, as theirs");
    const std::vector<std::string> told = {"Fine weather. They say you beat off the threat at the fortress today."};
    check::equal(all(lines.news_asked(chat, told)), std::string("Only the talk of what you did, and you'd tell that better than me."),
                 "told lately: the talk of it");
    check::equal(all(lines.news_asked(packets::small_talk("clear", "breezy"))), std::string("Nothing's reached me."), "none heard");
    check::is_true(lines.news_asked(packets::small_talk("fair", "still", "hostile")).empty(), "a cold speaker's small talk is their own");
    check::is_true(lines.news_asked(packets::greeted_having_heard()).empty(), "only small talk asks");
}

TEST(what_is_said_of_news_is_held_to_the_news_heard) {
    const auto flags = [](const std::string& reply, const FactPacket& p, const std::vector<std::string>& recent = {}) {
        return !communicator::untrue(reply, p, recent).empty();
    };
    // What came of Halbrann's work at the stranger's hands backs saying so: "They say you failed
    // Halbrann" was refused as the speaker's own work, and no line could pass a failure on.
    check::is_true(flags("They say you failed Halbrann.", packets::greeted_first()), "the premise: a failure nobody heard of");
    check::is_true(!flags("They say you failed Halbrann.", packets::greeted_having_heard_of("failed")), "a failure heard of");
    check::is_true(!flags("The word is you gave up Halbrann's work.", packets::small_talk_with_word_of("given_up", true)),
                   "work given up, heard of");
    check::is_true(flags("The word is you gave up Halbrann's work.", packets::small_talk_with_word_of("failed", true)),
                   "not a failure heard of as work given up");
    // "I heard that" passes news on only when a clause follows: "Aye, I heard that." overhears, and
    // "I've heard that excuse before" is a thing heard.
    // (Held to the news claim alone: a name the packet lacks is a fault of its own.)
    check::is_true(!flags_news("Aye, I heard that.", packets::greeted_first()), "overhearing, not news");
    check::is_true(!flags_news("I've heard that excuse before.", packets::greeted_first()), "a thing heard, not news");
    check::is_true(!flags_news("I heard that one.", packets::greeted_first()), "nor that one");
    check::is_true(flags_news("I heard that Wenweg held.", packets::greeted_first()), "the control: news of a place, with none heard");
    check::is_true(flags_news("I heard that the threat passed.", packets::greeted_first()), "and a clause with no name in it");
    // What is said of the stranger, only from someone who heard of what they did.
    check::is_true(flags_news("Nothing but what's said of you.", packets::greeted_first()), "talk of the stranger, with none heard");
    check::is_true(!flags_news("Nothing but what's said of you.", packets::greeted_having_heard()), "talk of the stranger, heard");
    // No news, only from someone who heard none to pass on.
    check::is_true(!flags("Nothing's reached me.", packets::small_talk("clear", "breezy")), "none heard");
    check::is_true(flags("Nothing's reached me.", packets::small_talk_with_news()), "news heard, and denied");
    // "As I said" is backed by news told lately.
    const FactPacket chat = packets::small_talk_with_news();
    check::is_true(flags("As I said, you beat off the threat at the fortress.", chat), "the premise: nothing said lately");
    check::is_true(!flags("As I said, you beat off the threat at the fortress.", chat, {"The word is you beat off the threat at the fortress."}),
                   "told lately");
}

TEST(the_voice_is_told_the_news_and_to_pass_it_on) {
    const auto system_of = [](const FactPacket& p) { return communicator::Voice::messages(p, "Hello.").front().content; };
    const std::string heard = system_of(packets::greeted_having_heard());
    check::is_true(heard.find("- News you have heard: the stranger helped Wenweg hold yesterday (word of it reached you).") != std::string::npos,
                   "the news, in C++'s words: " + heard);
    check::is_true(heard.find("say you heard of it") != std::string::npos, "and, in what just happened, to say so");
    const std::string plain = system_of(packets::greeted_first());
    check::is_true(plain.find("News") == std::string::npos && plain.find("heard of it") == std::string::npos, "the control: no news");
    const std::string chat = system_of(packets::small_talk_with_news());
    check::is_true(chat.find("pass on the news above") != std::string::npos, "small talk passes it on");
    FactPacket giver = packets::greeted_having_heard();
    giver.persona.name = "Halbrann";
    check::is_true(system_of(giver).find("News") == std::string::npos, "nothing of the giver's own work, which the memory says");
    // An answer about a place passes its news on, but not work only asked for there, which the
    // answer lists already: the written lines' rule, from the same chooser.
    const std::string about = system_of(packets::about_the_town_with_news());
    check::is_true(about.find("and the news of it above") != std::string::npos &&
                       about.find("- News you have heard: Orlan beat off the threat at the fortress") != std::string::npos,
                   "the premise: an answer about a place passes its news on: " + about);
    // Nor is it shown the work asked for there as news: shown both, the tuned model gave that work
    // to the wrong giver (2026-09-27).
    check::is_true(about.find("News you have heard: Halbrann") == std::string::npos, "not the work asked for, as news");
    FactPacket only_asked = packets::about_the_town();
    only_asked.facts.insert(only_asked.facts.end() - 1, packets::news_of_work("asked", false));
    check::is_true(system_of(only_asked).find("news of it") == std::string::npos, "not the work asked for there");
    // What happened to the speaker is not news they heard: no News line, and nothing to pass on.
    FactPacket saw = packets::small_talk_with_news();
    for (Fact& fact : saw.facts)
        for (auto& [key, value] : fact.fields)
            if (key == "how") value = "there";
    check::is_true(system_of(saw).find("News") == std::string::npos && system_of(saw).find("pass on") == std::string::npos,
                   "not what happened to them");
    // Old news is shown as what the speaker knows, dated, and not to be brought up unasked, as the
    // written lines do not.
    FactPacket old = packets::small_talk("clear", "breezy");
    Fact stale = packets::news_of_a_scare("beaten", "Orlan");
    for (auto& [key, value] : stale.fields)
        if (key == "since") value = "months";
    old.facts.insert(old.facts.end() - 1, stale);
    const std::string old_prompt = system_of(old);
    check::is_true(old_prompt.find("- News you have heard: Orlan beat off the threat at the fortress months back") != std::string::npos &&
                       old_prompt.find("pass on the news above") == std::string::npos,
                   "old news shown, dated, and not to be brought up: " + old_prompt);
    // A cold speaker is not told to pass it on, as their written lines do not.
    FactPacket cold = packets::greeted_having_heard();
    cold.facts.back() = packets::memory_of(cold.speaker, {.mood = "hostile"});
    check::is_true(system_of(cold).find("say you heard of it") == std::string::npos, "not a cold speaker");
}

TEST(news_is_claimed_only_with_a_news_fact) {
    const FactPacket none = packets::greeted_first();
    const FactPacket theirs = packets::greeted_having_heard();
    const FactPacket others = packets::about_the_town_with_news();
    check::is_true(flags_news("Word is the threat was beaten.", none), "news with none heard");
    check::is_true(flags_news("The word is the threat was beaten.", none), "the word, with none heard");
    check::is_true(!flags_news("Word is the threat was beaten.", others), "backed by news heard");
    check::is_true(flags_news("They say you helped Wenweg hold.", others), "the stranger's deed, with news of nothing they did");
    check::is_true(!flags_news("They say you helped Wenweg hold.", theirs), "backed by news of what they did");
    check::is_true(flags_news("Folk are saying the harvest failed.", none), "folk saying, with nothing heard");
    // What claims no news: a promise, and the words of a joiner or a memory.
    check::is_true(!flags_news("Done. My word is given.", none), "my word is a promise");
    check::is_true(!flags_news("You're the one who talked me down.", none), "a memory, not news");
    check::is_true(!flags_news("I heard you ask; I'll keep it short.", none), "hearing what was asked");
}

// Contract version 26: a hero's deed. Word of it said by those who heard it, in C++'s words; the hero's own
// told after their greeting or in small talk, in the first person, their side of it; never as news to
// themself, and not while said lately.
TEST(a_heros_deed_is_news_to_others_and_their_own_to_tell) {
    using communicator::dated_news_words;
    using communicator::dated_own_deed_words;
    using communicator::news_words;
    using communicator::own_deed_words;
    const auto deed = packets::news_of_a_heros_deed;
    // As word of it goes round.
    check::equal(news_words(deed("defended", "won", "word"), ""), std::string("Selbren held the fortress of Wenweg against the glacier dragons"),
                 "held");
    check::equal(news_words(deed("defended", "lost", "word"), ""),
                 std::string("Selbren was driven from the fortress of Wenweg by the glacier dragons"), "driven off");
    check::equal(news_words(deed("defended", "won", "word"), "", "you", "the fortress of Wenweg"),
                 std::string("Selbren held the line here against the glacier dragons"), "held, where the speaker stands");
    check::equal(news_words(deed("defended", "won", "word"), "", "you", "Wenweg"),
                 std::string("Selbren held the fortress against the glacier dragons"), "held, in the town the fortress is of");
    check::equal(news_words(deed("hunted", "won", "word"), ""), std::string("Selbren hunted down the glacier dragons"), "a hunt won");
    check::equal(news_words(deed("hunted", "lost", "word"), ""), std::string("Selbren went after the glacier dragons and was beaten back"),
                 "a hunt lost");
    check::equal(news_words(deed("assaulted", "won", "word"), ""), std::string("Selbren took the ruins of Linbrig from the glacier dragons"),
                 "an assault won");
    check::equal(news_words(deed("assaulted", "lost", "word"), ""),
                 std::string("Selbren was thrown back from the ruins of Linbrig by the glacier dragons"), "an assault lost");
    check::equal(dated_news_words(deed("hunted", "won", "word"), ""), std::string("Selbren hunted down the glacier dragons today"), "dated");
    check::equal(news_words(deed("defended", "won", "there"), "Selbren"), std::string(), "never as news to the hero themself");
    // Their own, in the first person.
    check::equal(own_deed_words(deed("defended", "won", "there"), "Selbren"),
                 std::string("we held the fortress of Wenweg against the glacier dragons"), "we held");
    check::equal(own_deed_words(deed("defended", "lost", "there"), "Selbren", "the fortress of Wenweg"),
                 std::string("we were driven off here by the glacier dragons"), "driven off here");
    check::equal(own_deed_words(deed("hunted", "won", "there"), "Selbren"), std::string("I hunted down the glacier dragons"), "I hunted");
    check::equal(own_deed_words(deed("hunted", "lost", "there"), "Selbren"),
                 std::string("I went after the glacier dragons and was beaten back"), "beaten back");
    check::equal(own_deed_words(deed("assaulted", "won", "there"), "Selbren"),
                 std::string("we took the ruins of Linbrig from the glacier dragons"), "we took");
    check::equal(own_deed_words(deed("assaulted", "lost", "there"), "Selbren"),
                 std::string("we were thrown back from the ruins of Linbrig by the glacier dragons"), "thrown back");
    check::equal(dated_own_deed_words(deed("defended", "won", "there"), "Selbren"),
                 std::string("we held the fortress of Wenweg against the glacier dragons today"), "dated");
    check::equal(own_deed_words(deed("defended", "won", "word"), "Fersel"), std::string(), "not another's deed");
    // Said after the greeting by the hero, once; not by a cold speaker.
    const Lines lines = Lines::parse("any | greeted | Good day.\n"
                                     "any | greeted_cold | What now.\n"
                                     "any | small_talk | Fine weather.\n"
                                     "any | own_deed_won | It came to steel: {own_deed}.\n"
                                     "any | own_deed_lost | It went badly: {own_deed}.\n"
                                     "any | news | The word is {news}.\n",
                                     "deeds");
    const std::string held = lines.speak(packets::greeted_after_own_deed("won"), "Hello.");
    check::equal(held, std::string("Good day. It came to steel: we held the fortress against the glacier dragons today."), "their side of it");
    check::equal(lines.speak(packets::greeted_after_own_deed("lost"), "Hello."),
                 std::string("Good day. It went badly: we were driven from the fortress by the glacier dragons today."), "and when it went badly");
    check::equal(lines.speak(packets::greeted_after_own_deed("won"), "Hello.", {held}), std::string("Good day."), "not while said lately");
    FactPacket cold = packets::greeted_after_own_deed("won");
    cold.facts.back() = packets::memory_of(cold.speaker, {.mood = "hostile"});
    check::equal(lines.speak(cold, "Hello."), std::string("What now."), "not from a cold speaker");
    // Heard of by someone else: news, in the third person.
    check::equal(lines.speak(packets::small_talk_with_a_heros_deed(), "Fine day."),
                 std::string("Fine weather. The word is Selbren held the line here against the glacier dragons today."), "word of it");
    // The voice is shown their own deed as theirs, and another's as news.
    const auto system_of = [](const FactPacket& p) { return communicator::Voice::messages(p, "Hello.").front().content; };
    const std::string own = system_of(packets::greeted_after_own_deed("won"));
    check::is_true(own.find("- What you did yourself: we held the fortress against the glacier dragons today.") != std::string::npos,
                   "their own, to the voice: " + own);
    check::is_true(own.find("News you have heard") == std::string::npos, "not as news");
    const std::string heard = system_of(packets::small_talk_with_a_heros_deed());
    check::is_true(heard.find("- News you have heard: Selbren held the line here against the glacier dragons today") != std::string::npos,
                   "another's, as news: " + heard);
    // Claimed only with their own deed in the packet: the truth check.
    const auto flags_own = [](const std::string& reply, const FactPacket& p) {
        for (const std::string& problem : communicator::untrue(reply, p, {}))
            if (problem.rfind("a fight of the speaker's own", 0) == 0) return true;
        return false;
    };
    check::is_true(!flags_own(held, packets::greeted_after_own_deed("won")), "theirs: true");
    check::is_true(flags_own("We held the fortress against them, a few days back.", packets::greeted_first()), "none of theirs: untrue");
    check::is_true(flags_own("I hunted down the beasts myself.", packets::small_talk_with_a_heros_deed()), "another's deed is not theirs");
    check::is_true(!flags_own("We held a feast for the harvest.", packets::greeted_first()), "a feast is no fight");
}
