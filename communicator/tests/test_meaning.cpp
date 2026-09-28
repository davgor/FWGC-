// What the stranger's words are about, by meaning, with the embedding model (embedder.hpp), and
// small talk that answers it.
#include "communicator/meaning.hpp"

#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "communicator/authored.hpp"
#include "embedder.hpp"
#include "model.hpp"
#include "packets.hpp"

namespace {

using communicator::Lines;
using communicator::Meaning;
using orchestrator::ActKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

// Small talk in fair weather: two lines of the weather, two of the speaker, said alike.
const Lines& small_talk_lines() {
    static const Lines lines = Lines::parse("any | small_talk_fair | It's clear and bright; not a cloud in the sky this {time}.\n"
                                            "any | small_talk_fair | It's dry and fair, for {season}.\n"
                                            "any | small_talk | I'm tired, but well enough, thank you for asking.\n"
                                            "any | small_talk | I'm busy as ever; the work never stops.\n"
                                            "any | small_talk_cold | I've no time for talk.\n"
                                            "any | parted | Farewell.\nany | parted | Safe travels.\n",
                                            "test");
    return lines;
}

// Small talk the speaker has heard `heard` times before, taken as `mood`.
FactPacket chat(int heard, const std::string& mood = "neutral") {
    FactPacket p = packets::small_talk("clear", "still", mood);
    p.facts.back() = packets::memory_of(p.speaker, {.met = heard > 0 ? "again" : "first", .times = heard, .mood = mood, .heard = heard});
    return p;
}

bool of_the_weather(const std::string& reply) {
    return reply.find("cloud") != std::string::npos || reply.find("dry and fair") != std::string::npos;
}

// Small talk a player might make, and its subject: 0 the weather, 1 the speaker's own day, 2 news.
const std::vector<std::pair<std::string, std::size_t>> kSmallTalk = {
    {"Still cold out? Looks like rain.", 0}, {"How's the weather treating you?", 0}, {"Nice weather.", 0},
    {"Think it'll rain?", 0},                {"Bit wet today, isn't it?", 0},      {"How are you keeping these days?", 1},
    {"How are things with you?", 1},         {"Busy today?", 1},                    {"So, how are things really?", 1},
    {"How's business?", 1},                  {"Any news?", 2},                      {"Heard anything lately?", 2},
    {"What's the word around here?", 2},     {"What are people saying these days?", 2}};

// As Lines::speak() words them (contract version 11 added news).
const std::vector<std::string> kSubjects = {"The weather: rain, sun, wind, cold, heat, a storm, a fine day or a wet one.",
                                            "How someone is keeping: their health, their mood, their work, their day.",
                                            "News and rumours: what people are saying, word from elsewhere, anything that has happened."};

}  // namespace

TEST(closeness_is_one_number_a_text_and_nearer_for_the_nearer_meaning) {
    Meaning meaning(test_model::embedder());
    const std::vector<std::string> texts = {"It looks like rain before evening.", "My knees ache from kneeling in the chapel."};
    const std::vector<float> close = meaning.closeness("Is it going to rain today?", texts);
    check::equal(close.size(), texts.size(), "one number a text");
    check::is_true(close[0] > close[1], "the weather nearer a question of the weather");
    for (const float c : close) check::is_true(c >= -1.01f && c <= 1.01f, "a cosine");
    check::is_true(meaning.closeness("Is it going to rain today?", texts) == close, "the same numbers again, from the kept vectors");
}

TEST(small_talk_is_read_as_the_weather_the_speakers_own_day_or_news) {
    Meaning meaning(test_model::embedder());
    for (const auto& [said, subject] : kSmallTalk) check::equal(meaning.subject_of(said, kSubjects), subject, "the subject of: " + said);
    bool refused = false;
    try {
        (void)meaning.subject_of("Hello.", {});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    check::is_true(refused, "no subjects, no choice");
}

TEST(the_strangers_words_choose_what_small_talk_is_about) {
    const Lines& lines = small_talk_lines();
    check::equal(lines.replies(chat(0)).size(), std::size_t(4), "the premise: the weather's lines and the rest, said alike");
    // The control: in turn, as without meaning, a question of health gets the weather some time.
    std::set<bool> in_turn;
    for (int heard = 0; heard < 4; ++heard) in_turn.insert(of_the_weather(lines.speak(chat(heard), "How are you keeping these days?")));
    check::equal(in_turn.size(), std::size_t(2), "the premise: in turn, the weather to a question of health");
    Meaning meaning(test_model::embedder());
    // News asked of a bank with no news lines is answered from the speaker's own day.
    for (const auto& [said, subject] : kSmallTalk)
        for (int heard = 0; heard < 3; ++heard) {
            const std::string reply = lines.speak(chat(heard), said, {}, &meaning);
            check::equal(of_the_weather(reply), subject == 0, "\"" + said + "\" answered: " + reply);
        }
}

TEST(asked_for_news_small_talk_answers_with_it) {
    // "Any news?" drew the morning light from a speaker with news to give (judged 2026-09-27).
    const Lines lines = Lines::parse("any | small_talk_fair | It's dry and fair, for {season}.\n"
                                     "any | small_talk | I'm busy as ever; the work never stops.\n"
                                     "any | heard_deed | They say you {deed}.\n"
                                     "any | heard_deed_told | Only the talk of what you did, and you'd tell that better than me.\n"
                                     "any | news | The word is {news}.\n"
                                     "any | news_told | As I said, {news}.\n"
                                     "any | no_news | Nothing's reached me.\n",
                                     "test");
    Meaning meaning(test_model::embedder());
    const FactPacket heard = packets::small_talk_with_news();
    const std::string deed = "They say you beat off the threat at the fortress today.";
    check::equal(lines.speak(heard, "Nice weather.", {}, &meaning), "It's dry and fair, for deep winter. " + deed,
                 "the premise: of the weather, the weather, then what the stranger did");
    const FactPacket others = packets::small_talk_with_word_of("asked", false);
    const std::string work = "The word is Halbrann wants someone to stand with Wenweg when they come for it.";
    for (const std::string asked : {"Any news?", "Heard anything lately?"}) {
        check::equal(lines.speak(heard, asked, {}, &meaning), deed, "\"" + asked + "\": the stranger's deed, said alone");
        check::equal(lines.speak(heard, asked, {deed}, &meaning), std::string("Only the talk of what you did, and you'd tell that better than me."),
                     "\"" + asked + "\", told it lately: the talk of it");
        check::equal(lines.speak(others, asked, {}, &meaning), work, "\"" + asked + "\": others' news, said alone");
        check::equal(lines.speak(others, asked, {work}, &meaning),
                     std::string("As I said, Halbrann wants someone to stand with Wenweg when they come for it."),
                     "\"" + asked + "\", told it lately: no more than was told");
        check::equal(lines.speak(packets::small_talk("clear", "breezy"), asked, {}, &meaning), std::string("Nothing's reached me."),
                     "\"" + asked + "\", with none heard");
    }
}

TEST(on_its_subject_the_next_line_each_time_it_is_heard) {
    Meaning meaning(test_model::embedder());
    const Lines& lines = small_talk_lines();
    const std::string said = "How are you keeping these days?";
    const std::string first = lines.speak(chat(0), said, {}, &meaning);
    const std::string second = lines.speak(chat(1), said, {}, &meaning);
    check::is_true(first != second, "the next line the next time");
    check::equal(lines.speak(chat(2), said, {}, &meaning), first, "then round again");
    check::equal(lines.speak(chat(0), said, {first}, &meaning), second, "never the last reply twice while another fits");
    // Two subjects asked in turn: each comes round its own lines, whatever the count of small talk.
    std::vector<std::string> lately;
    std::set<std::string> own_day, weather;
    for (int heard = 0; heard < 8; ++heard) {
        const std::string reply = lines.speak(chat(heard), heard % 2 == 0 ? said : "Nice weather today.", lately, &meaning);
        (heard % 2 == 0 ? own_day : weather).insert(reply);
        lately.push_back(reply);
    }
    check::equal(own_day.size(), std::size_t(2), "both of the speaker's lines, asked every other time");
    check::equal(weather.size(), std::size_t(2), "and both of the weather's");
    // No words to go by: in turn, as without meaning.
    check::equal(lines.speak(chat(1), "", {}, &meaning), lines.speak(chat(1), ""), "nothing said, nothing to compare");
}

TEST(meaning_chooses_only_small_talk_and_never_a_cold_speakers) {
    Meaning meaning(test_model::embedder());
    const Lines& lines = small_talk_lines();
    check::equal(lines.speak(chat(0, "hostile"), "Nice weather.", {}, &meaning), std::string("I've no time for talk."), "cold, whatever the subject");
    FactPacket bye = packets::parted();
    for (int heard = 0; heard < 2; ++heard) {
        bye.facts.back() = packets::memory_of(bye.speaker, {.heard = heard});
        check::equal(lines.speak(bye, "Nice weather, goodbye.", {}, &meaning), lines.speak(bye, "Nice weather, goodbye."), "a farewell goes in turn");
    }
}

TEST(asked_to_be_remembered_a_speaker_recalls_the_meeting) {
    const Lines lines = Lines::parse("any | greeted_again | Hello again.\n"
                                     "any | greeted_again | Good day.\n"
                                     "any | greeted_again | Back again. I saw you {since}.\n",
                                     "test");
    const auto greeting = [](int heard, const std::string& mood = "neutral") {
        FactPacket p = packets::greeted_again();
        p.facts.back() = packets::memory_of(p.speaker, {.met = "again", .times = heard + 1, .since = "weeks", .mood = mood, .heard = heard});
        return p;
    };
    // The control: in turn, a plea to be remembered gets a bare greeting some time.
    std::set<std::string> in_turn;
    for (int heard = 0; heard < 3; ++heard) in_turn.insert(lines.speak(greeting(heard), "Remember me?"));
    check::equal(in_turn.size(), std::size_t(3), "the premise: in turn, every greeting");
    Meaning meaning(test_model::embedder());
    for (const std::string asked : {"Remember me?", "It's been a long time, hasn't it?", "Do you remember me?"})
        for (int heard = 0; heard < 3; ++heard)
            check::equal(lines.speak(greeting(heard), asked, {}, &meaning), std::string("Back again. I saw you some weeks ago."),
                         "asked \"" + asked + "\": the meeting recalled");
    std::set<std::string> plain;
    for (int heard = 0; heard < 3; ++heard) plain.insert(lines.speak(greeting(heard), "Good morning.", {}, &meaning));
    check::equal(plain.size(), std::size_t(3), "a plain greeting still goes in turn");
    // Recalled lately, the same recollection is not said again: the greeting goes in turn.
    const std::vector<std::string> lately = {"Back again. I saw you some weeks ago."};
    const std::string next = lines.speak(greeting(1), "Remember me?", lately, &meaning);
    check::is_true(next != lately.front() && !next.empty(), "not the same recollection twice running: " + next);
    check::is_true(lines.speak(greeting(0, "hostile"), "Remember me?", {}, &meaning).empty(), "a cold speaker has no line here to recall with");
    // News of what the stranger did comes after the recollection, as after any greeting (contract
    // version 11).
    const Lines with_news = Lines::parse("any | greeted_again | Back again. I saw you {since}.\n"
                                         "any | heard_deed | You're the one who {deed}, aren't you?\n",
                                         "test");
    FactPacket heard = greeting(0);
    heard.facts.insert(heard.facts.end() - 1, packets::news_of_work("finished", true));
    check::equal(with_news.speak(heard, "Remember me?", {}, &meaning),
                 std::string("Back again. I saw you some weeks ago. You're the one who helped us hold yesterday, aren't you?"),
                 "the meeting recalled, then what they heard");
}

TEST(a_model_loaded_for_text_cannot_compare_meanings) {
    Meaning meaning(test_model::model());
    bool refused = false;
    try {
        (void)meaning.closeness("Hello.", {"Hello."});
    } catch (const inference::Error&) {
        refused = true;
    }
    check::is_true(refused, "refused, by inference");
}
