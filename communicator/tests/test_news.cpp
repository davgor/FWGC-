// News a speaker has heard (contract version 11): the words C++ says it in, the written line said
// after a reply, the voice told to pass it on, and the truth check holding news to a News fact. No
// model.
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
    // Not the stranger's doing: no deed, and the work as asked.
    check::equal(deed_words(packets::news_of_work("asked", false), ""), std::string(), "asked is nobody's deed");
    check::equal(news_words(packets::news_of_work("asked", false), ""), std::string("Halbrann is looking for help with Wenweg"), "asked");
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
    // Nothing of the speaker's own work, or of themself: their own lines say that.
    check::equal(news_words(packets::news_of_work("finished", true), "Halbrann"), std::string(), "not to the giver");
    check::equal(news_words(exposed("yes"), "Maren"), std::string(), "not to the one unmasked");
}

TEST(news_is_said_after_the_reply_once_and_never_by_the_cold_or_the_frightened) {
    const Lines& lines = news_lines();
    check::equal(lines.speak(packets::greeted_having_heard(), "Hello."),
                 std::string("Good day. You're the one who helped Wenweg hold, aren't you?"), "a greeting, then what they heard of the stranger");
    check::equal(lines.speak(packets::small_talk_with_news(), "Fine day."),
                 std::string("Fine weather. The word is you beat off the threat at the fortress of Wenweg."), "small talk, then the news");
    // After an answer about a place, its news, but not the work asked for there, which the answer
    // lists already; small talk, which lists nothing, passes that on.
    const std::string about = lines.speak(packets::about_the_town_with_news(), "Tell me about Wenweg.");
    const std::string then = " The word is Orlan beat off the threat at the fortress of Wenweg.";
    check::is_true(about.size() > then.size() && about.compare(about.size() - then.size(), then.size(), then) == 0,
                   "an answer about a place, then its news: " + about);
    FactPacket only_asked = packets::about_the_town();
    only_asked.facts.insert(only_asked.facts.end() - 1, packets::news_of_work("asked", false));
    check::equal(lines.follow(only_asked), std::string(), "not the work asked for, after an answer that lists it");
    FactPacket chat_asked = packets::small_talk("clear", "breezy");
    chat_asked.facts.insert(chat_asked.facts.end() - 1, packets::news_of_work("asked", false));
    check::equal(lines.follow(chat_asked), std::string("The word is Halbrann is looking for help with Wenweg."),
                 "but small talk passes it on");
    // The controls: no news, no sentence after.
    check::equal(lines.speak(packets::greeted_first(), "Hello."), std::string("Good day."), "no news, nothing more");
    // Told lately, not told again, however it was put: said in a greeting as what the stranger did,
    // it is not said again in small talk as news (the first news session, 2026-09-27).
    const std::string greeted = "Good day. You're the one who helped Wenweg hold, aren't you?";
    check::equal(lines.speak(packets::greeted_having_heard(), "Hello.", {greeted}), std::string("Good day."),
                 "news said lately is not said again");
    FactPacket chat = packets::small_talk("clear", "breezy");
    chat.facts.insert(chat.facts.end() - 1, packets::news_of_work("finished", true));
    check::equal(lines.speak(chat, "Any news?"), std::string("Fine weather. The word is you helped Wenweg hold."),
                 "the premise: small talk passes it on");
    check::equal(lines.speak(chat, "Any news?", {greeted}), std::string("Fine weather."), "not after the greeting said it");
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

TEST(the_voice_is_told_the_news_and_to_pass_it_on) {
    const auto system_of = [](const FactPacket& p) { return communicator::Voice::messages(p, "Hello.").front().content; };
    const std::string heard = system_of(packets::greeted_having_heard());
    check::is_true(heard.find("- News you have heard: the stranger helped Wenweg hold (word of it reached you).") != std::string::npos,
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
    check::is_true(system_of(packets::about_the_town_with_news()).find("and the news of it above") != std::string::npos,
                   "the premise: an answer about a place passes its news on");
    FactPacket only_asked = packets::about_the_town();
    only_asked.facts.insert(only_asked.facts.end() - 1, packets::news_of_work("asked", false));
    check::is_true(system_of(only_asked).find("news of it") == std::string::npos, "not the work asked for there");
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
