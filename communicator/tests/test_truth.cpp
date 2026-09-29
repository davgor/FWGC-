// The truth checker: every claim a reply makes, held to its packet and its speaker's memory. Each
// kind of claim is shown backed by one packet and unbacked by another, so a check that stopped
// looking would pass the first and fail the second.
#include "communicator/truth.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "check.hpp"
#include "news.hpp"
#include "packets.hpp"
#include "text.hpp"
#include "them.hpp"

namespace {

using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

// Whether `reply` has a problem of `kind` ("a name no fact gives") for `p`.
bool flags(const std::string& reply, const FactPacket& p, const std::string& kind, const std::vector<std::string>& recent = {}) {
    const std::vector<std::string> problems = communicator::untrue(reply, p, recent);
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& problem) { return problem.rfind(kind + ":", 0) == 0; });
}

bool clean(const std::string& reply, const FactPacket& p, const std::vector<std::string>& recent = {}) {
    return communicator::untrue(reply, p, recent).empty();
}

}  // namespace

TEST(a_name_must_be_one_the_packet_gives) {
    check::is_true(clean("Selbren keeps the fortress of Wenweg, a long walk away.", packets::work()), "the packet's names, and I");
    check::is_true(flags("Ask Gandalf about it.", packets::work(), "a name no fact gives"), "a name no fact gives");
    check::is_true(clean("Apology accepted, then.", packets::forgiven("neutral")), "a fixed opening may begin the reply");
    check::is_true(flags("Ask for the one they call Hello.", packets::work(), "a name no fact gives"),
                   "a word sentences begin with, standing as a name past the start");
    check::is_true(clean("Tornar\xE2\x80\x99s the one to ask.", packets::about_the_town()), "a curly apostrophe is an apostrophe");
    // The stance a cold speaker's model reply opens with is no name (a voice reply was thrown away
    // for "Hmph", 2026-09-26); past a sentence's start it is one.
    const FactPacket cold = packets::small_talk("fair", "still", "hostile");
    check::is_true(clean("Hmph. The wind is still.", cold), "the stance's opening");
    check::is_true(flags("Hmph. The wind is still.", packets::small_talk("fair", "still"), "a name no fact gives"),
                   "the control: from a neutral speaker, no stance, so a name");
    check::is_true(flags("The wind, Hmph, is still.", cold, "a name no fact gives"), "past a sentence's start");
}

TEST(a_distance_or_a_time_is_never_counted_out) {
    const FactPacket safety = packets::safety_unsafe();
    check::is_true(clean("Keep clear of the hospice on the Wenweg pass, an hour's walk away.", safety), "a range in the packet's words");
    for (const std::string reply : {"It's three days' march to the hospice.", "The hospice is two miles off.",
                                    "The hospice is a day's ride from here.", "I've watched it for 20 years."})
        check::is_true(flags(reply, safety, "a distance or a time counted out"), "counted out: " + reply);
    check::is_true(clean("One day you'll see it for yourself.", safety), "\"one day\", the idiom");
    check::is_true(clean("Hello again, and on your own two feet, I'm glad to see.", packets::greeted_again()), "standing, not walking");
    check::is_true(clean("Think nothing of it; it's all in a day's work.", packets::thanked()), "a day's work goes nowhere");
    check::is_true(flags("Think nothing of it; it's all in a day's walk.", packets::thanked(), "a distance or a time counted out"),
                   "a day's walk does");
    // When they met, as the memory says it, is not counted out.
    check::is_true(clean("I saw you a few days ago.", packets::greeted_again()), "a few days ago, as the memory has it");
    check::is_true(communicator::never_backed("a distance or a time counted out: three days"), "never backed");
}

TEST(a_range_must_be_one_a_fact_holds) {
    check::is_true(clean("Nerstrann is a long walk away.", packets::surroundings()), "a range a fact holds");
    check::is_true(flags("The hospice is a long way off.", packets::safety_unsafe(), "a distance no fact holds"),
                   "a range no fact holds");
    // Each range in the packet's own words, held by a place at that range and by no other.
    for (const std::string range : {"here", "close", "near", "walk", "far", "distant"}) {
        const auto at = [](const std::string& where) {
            return packets::packet(packets::castellan(), ActKind::AskAboutPlace, Outcome::Answered,
                                   {{FactKind::Place, "keyloc-smelter_ruin-180", {{"name", "the burnt works"}, {"range", where}}}});
        };
        const std::string reply = "That's the burnt works, " + communicator::text::range_words(range) + ".";
        check::is_true(clean(reply, at(range)), "held: " + reply);
        check::is_true(flags(reply, at(range == "here" ? "distant" : "here"), "a distance no fact holds"), "not held: " + reply);
    }
    check::is_true(flags("The deeps are an hour's walk from here.", packets::safety_safe(), "a distance no fact holds"),
                   "a range's words without \"away\"");
    // Where a person is, not a place.
    check::is_true(flags("The hospice is right here.", packets::declined(), "a distance no fact holds"), "a place right here");
    check::is_true(clean("Fair enough; I'm right here if you change your mind.", packets::declined()), "the speaker right here");
    check::is_true(clean("Yes, you're all right here.", packets::declined()), "all right, here");
    check::is_true(clean("Give the dragons a wave from a long way off.", packets::safety_unsafe()), "how far to keep");
}

TEST(a_danger_word_needs_a_place_or_nest_that_has_it) {
    check::is_true(clean("The Glacier dragons are deadly.", packets::safety_unsafe()), "a nest at the top of the scale");
    check::is_true(flags("It's deadly out there.", packets::greeted_first(), "a danger no place or nest has"), "nothing deadly");
    check::is_true(clean("It's dangerous out there.", packets::safety_unsafe()), "a verdict that it is not safe");
    check::is_true(flags("It's dangerous out there.", packets::small_talk("clear", "still"), "a danger no place or nest has"),
                   "a nest of three is not dangerous");
    check::is_true(flags("The keep is deadly.", packets::safety_wary(), "a danger no place or nest has"), "a place of four is not deadly");
    check::is_true(clean("The keep is dangerous.", packets::safety_wary()), "but dangerous");
    // A verdict that it is not safe, resting on nests of three, lets the speaker call it dangerous.
    const FactPacket unsafe_nests = packets::packet(
        packets::gate_captain(), ActKind::AskSafety, Outcome::Answered,
        {{FactKind::Safety, "", {{"verdict", "unsafe"}}},
         {FactKind::Danger, "nest-hippogryphs-179", {{"creature", "Hippogryphs"}, {"danger", "3"}, {"range", "walk"}}}});
    check::is_true(clean("It's dangerous out there.", unsafe_nests), "an unsafe verdict");
}

TEST(when_they_met_and_whether_they_have_are_the_memorys) {
    const FactPacket again = packets::greeted_again(), first = packets::greeted_first();
    check::is_true(clean("Hello again; I saw you a few days ago.", again), "a few days ago, as remembered");
    check::is_true(flags("Hello again; I saw you yesterday.", again, "when they met, which the speaker's memory does not say"),
                   "yesterday, remembered as days");
    check::is_true(flags("I saw you some weeks ago.", first, "when they met, which the speaker's memory does not say"),
                   "a first meeting has no since");
    check::is_true(clean("Welcome back.", again), "back, met before");
    check::is_true(flags("Welcome back.", first, "a meeting the speaker's memory does not hold"), "back, never met");
    check::is_true(flags("A new face, is it?", again, "a first meeting, which the speaker's memory contradicts"), "new, met before");
    check::is_true(clean("A new face, is it?", first), "new, never met");
    // A time in a sentence of nothing met, and first meetings said as such.
    check::is_true(clean("My word stands today as it stood yesterday.", again), "yesterday, of no meeting");
    check::is_true(clean("Good day. I don't think we've met.", first), "not met, said so");
    check::is_true(flags("Good day. I don't think we've met.", again, "a first meeting, which the speaker's memory contradicts"),
                   "not met, when they have");
    check::is_true(flags("Good day. We've met, I think.", first, "a meeting the speaker's memory does not hold"), "met, never met");
    check::is_true(clean("Have we met? Forgive me if we have.", first) && clean("Have we met? Forgive me if we have.", again), "asked");
    // Again, of telling or of an invitation.
    check::is_true(clean("I'll gladly tell you again.", first), "telling again");
    check::is_true(flags("You again. What now?", first, "a meeting the speaker's memory does not hold"), "you again, never met");
    check::is_true(clean("Go well; you'll be welcome back.", first), "an invitation");
    check::is_true(clean("Do come back again.", first), "come back again");
    check::is_true(clean("Safe travels; the fire will be going when you're back.", first), "a time to come");
    check::is_true(flags("So you're back.", first, "a meeting the speaker's memory does not hold"), "back, never met");
    // A frightened greeting is said whether or not they have met: "hello again" is the memory's.
    check::is_true(flags("Hello again. Forgive me, I'm shaking.", first, "a meeting the speaker's memory does not hold"),
                   "hello again, never met");
    check::is_true(clean("Hello again. Forgive me, I'm shaking.", again), "hello again, met before");
    check::is_true(flags("Back again, are you?", first, "a meeting the speaker's memory does not hold"), "back again, never met");
    check::is_true(flags("The same wrong one as last time.", packets::denied(false), "a meeting the speaker's memory does not hold"),
                   "last time, never met");
    check::is_true(clean("The same wrong one as last time.", packets::denied(true)), "last time, the same act before");
}

TEST(as_i_told_you_needs_something_told) {
    const FactPacket safety = packets::safety_unsafe();
    const std::string reply = "Not safe, as I told you: keep off the hospice.";
    check::is_true(flags(reply, safety, "something told before, which the speaker has not said"), "nothing said lately");
    check::is_true(clean(reply, safety, {"Keep clear of the hospice on the Wenweg pass, an hour's walk away."}),
                   "the hospice said lately");
    check::is_true(clean("As I said, the nearest is the ninth stone on the Linbrig road, a short walk away.", packets::surroundings_again()),
                   "asked twice in one conversation");
    // Words that point back, and words that do not.
    check::is_true(flags("No, same as before.", safety, "something told before, which the speaker has not said"), "same as before");
    check::is_true(clean("No, same as before.", safety, {"Keep clear of the Glacier dragons."}), "same as before, said lately");
    check::is_true(clean("Keep off the hospice, and remember I told you.", safety), "told, of what is said now");
    check::is_true(clean("Good day. You are as welcome as before.", packets::greeted_again()), "as before, of a meeting");
    check::is_true(flags("Good day. You are as welcome as before.", packets::greeted_first(),
                         "something told before, which the speaker has not said"),
                   "as before, never met");
    check::is_true(clean("Keep off the hospice; take that exactly as I said it.", safety), "as I said it");
    // Work told lately (contract version 13): another's by the giver's name, the speaker's own by its
    // purpose; and "since I told you" points back too.
    const FactPacket others = packets::work_at_the_inn();
    check::is_true(flags("Nothing new since I told you.", others, "something told before, which the speaker has not said"),
                   "since I told you, nothing said lately");
    check::is_true(clean("Nothing new since I told you.", others,
                         {"No work of mine, but Dornar of the council wants someone to walk the walls of Torhas with them."}),
                   "since I told you, a giver named lately");
    const FactPacket own = packets::work();
    const std::string again = "It's as I told you: hold the fortress above Wenweg with me.";
    check::is_true(flags(again, own, "something told before, which the speaker has not said"), "their own work, nothing said lately");
    check::is_true(clean(again, own, {"There's work: hold the fortress above Wenweg with me, fair work, a long walk away."}),
                   "their own work, said lately");
    check::is_true(flags(again, own, "something told before, which the speaker has not said", {"I'm Selbren, castellan here."}),
                   "their own name is not their work");
    // What they are after, told lately (contract version 13): "... above Iskmor, as I said" (2026-09-28).
    const FactPacket them = packets::about_them();
    const std::string aim = "I mean to hold the fortress above Wenweg, as I said.";
    check::is_true(flags(aim, them, "something told before, which the speaker has not said"), "what they are after, nothing said lately");
    check::is_true(clean(aim, them, {"I'm Selbren, and I mean to hold the fortress above Wenweg."}), "what they are after, said lately");
}

TEST(the_strangers_words_and_what_was_told_before_are_the_memorys) {
    // Contract version 12: "you told me" only where the speaker kept the stranger's words.
    const std::string words = "Hello again. You told me once: your sister keeps the mill at the ford.";
    check::is_true(clean(words, packets::greeted_remembering()), "the stranger's words, kept");
    check::is_true(flags(words, packets::greeted_again(), "the stranger's own words, which the speaker's memory does not hold"),
                   "none kept");
    check::is_true(flags("Good day. Last time you said you'd be back.", packets::greeted_again(),
                         "the stranger's own words, which the speaker's memory does not hold"),
                   "last time you said, none kept");
    check::is_true(flags("Good day. I remember what you told me: the mill.", packets::greeted_again(),
                         "the stranger's own words, which the speaker's memory does not hold"),
                   "what you told me, none kept");
    // Words of what the stranger says now claim nothing kept.
    check::is_true(clean("Good day. As you say, the weather holds.", packets::greeted_again()), "what they say now");
    check::is_true(clean("Thank you. I'm glad you said yes.", packets::accepted()), "the yes just said");
    check::is_true(clean("A pity, but you told me straight, and that counts.", packets::abandoned()), "told straight, just now");
    check::is_true(clean("Good to see you. You did what you said you would.", packets::greeted_after("finished", "warm")),
                   "what they said they would do, done");
    // "As I told you", backed by telling the stranger of it before, when the Recall fact says.
    const FactPacket told = packets::about_the_town_told();
    const std::string lead = "As I told you some weeks ago, that's Wenweg, right here.";
    check::is_true(clean(lead, told), "told some weeks ago, as remembered");
    check::is_true(flags(lead, packets::about_the_town(), "something told before, which the speaker has not said"),
                   "the control: nothing told");
    check::is_true(flags("As I told you yesterday, that's Wenweg, right here.", told,
                         "when they met, which the speaker's memory does not say"),
                   "told yesterday, which the Recall fact does not say");
    // Told a few days ago, by a speaker who met the stranger yesterday: backed, and nothing counted.
    FactPacket days = packets::about_the_town_told("days");
    days.facts.back() = packets::memory_of(days.speaker, {.met = "again", .times = 2, .since = "yesterday"});
    check::is_true(clean("As I told you a few days ago, that's Wenweg, right here.", days),
                   "a few days ago, as the Recall fact says it: " + (communicator::untrue("As I told you a few days ago, that's Wenweg, right here.", days).empty()
                                                                         ? std::string()
                                                                         : communicator::untrue("As I told you a few days ago, that's Wenweg, right here.", days).front()));
    check::is_true(flags("I told you three days ago.", days, "a distance or a time counted out"), "a time counted out, still");
}

TEST(dated_news_counts_no_time_out_in_any_band) {
    // "Helped Wenweg hold a few days back" says when, as the News fact dates it: read as a time
    // counted out, it had silenced every dated line of news in that band (2026-09-27).
    for (const std::string since : {"today", "yesterday", "days", "weeks", "months", "years"}) {
        FactPacket heard = packets::greeted_having_heard();
        for (Fact& f : heard.facts)
            if (f.kind == FactKind::News)
                for (auto& [key, value] : f.fields)
                    if (key == "since") value = since;
        const Fact* news = nullptr;
        for (const Fact& f : heard.facts)
            if (f.kind == FactKind::News) news = &f;
        check::is_true(news != nullptr, "the premise: news");
        const std::string deed = communicator::dated_deed_words(*news, communicator::text::spoken(heard.persona.name),
                                                                communicator::text::spoken(heard.persona.place));
        check::is_true(!deed.empty(), "the premise: words for the deed");
        const std::string line = "Hello. You're the one who " + deed + ", aren't you?";
        const std::vector<std::string> problems = communicator::untrue(line, heard);
        check::is_true(problems.empty(), since + ": " + line + (problems.empty() ? std::string() : " -- " + problems.front()));
    }
}

TEST(the_speakers_own_work_is_the_memorys) {
    check::is_true(clean("Welcome back; the work's done, and done well.", packets::greeted_after("finished", "warm")), "finished");
    check::is_true(flags("Welcome back; the work's done, and done well.", packets::greeted_after("taken", "friendly"),
                         "the speaker's own work, which their memory does not say"),
                   "done, when only taken");
    check::is_true(flags("Hello again. You gave up on me.", packets::greeted_again(), "the speaker's own work, which their memory does not say"),
                   "given up, when nothing was taken");
    // The work in hand: given up now, or held by the stranger who asks the wrong speaker to release it.
    check::is_true(clean("A pity, but you gave it up openly.", packets::abandoned()), "given up now");
    const FactPacket release = packets::packet(packets::castellan(), ActKind::AbandonQuest, Outcome::Refused,
                                               {Fact{FactKind::Reason, "", {{"why", "not_theirs_to_release"}}}});
    check::is_true(clean("I won't; you took it on, so it's yours to finish.", release), "held, asked of the wrong speaker");
    check::is_true(flags("I won't; you took it on, so it's yours to finish.", packets::declined(),
                         "the speaker's own work, which their memory does not say"),
                   "never taken");
    // A wish is not a claim.
    const FactPacket set_down = packets::greeted_after("abandoned", "wary");
    check::is_true(clean("Well met again. I'd sooner you finished what you started.", set_down), "a wish");
    check::is_true(flags("Well met again. You finished what you started.", set_down, "the speaker's own work, which their memory does not say"),
                   "a claim");
}

TEST(a_verdict_is_not_said_against_itself) {
    check::is_true(clean("Not safe. Keep off the hospice.", packets::safety_unsafe()), "unsafe, said so");
    check::is_true(flags("No; though it's safe enough by day.", packets::safety_unsafe(), "words against the verdict"),
                   "unsafe, and safe enough");
    check::is_true(flags("Yes; not safe for the timid, mind.", packets::safety_safe(), "words against the verdict"), "safe, and not safe");
}

TEST(small_talks_weather_time_and_season_are_the_packets) {
    const FactPacket clear = packets::small_talk("clear", "breezy"), wet = packets::small_talk("unsettled", "breezy"),
                     storm = packets::small_talk("stormy", "a gale");
    check::is_true(clean("This rain won't let up.", wet), "rain in the wet");
    check::is_true(flags("This rain won't let up.", clear, "weather the packet does not report"), "rain on a clear day");
    check::is_true(clean("What a gale this is.", storm), "a gale in a storm");
    check::is_true(flags("What a gale this is.", wet, "weather the packet does not report"), "a gale in the wet");
    check::is_true(clean("Such clear skies.", clear), "clear skies on a clear day");
    check::is_true(flags("Such clear skies.", wet, "weather the packet does not report"), "clear skies in the wet");
    check::is_true(clean("It's cold this evening.", clear), "the evening it is");
    check::is_true(flags("It's cold this morning.", clear, "a time of day or season the packet does not report"), "morning, in the evening");
    check::is_true(flags("This summer's been long.", clear, "a time of day or season the packet does not report"), "summer, in deep winter");
    // Only small talk reports the weather: elsewhere rain is a figure of speech.
    check::is_true(clean("Well met again; you're as welcome here as rain in a dry spell.", packets::greeted_again()), "a figure of speech");
}

TEST(an_acquaintance_with_someone_only_named_is_never_backed) {
    const FactPacket town = packets::about_the_town();
    const std::vector<std::string> problems = communicator::untrue("You'll find Tornar of the council there; give them my regards.", town);
    check::is_true(problems.size() == 1 && communicator::never_backed(problems.front()), "greetings sent: never backed");
    check::is_true(flags("Ask for Tornar, and tell me how they're keeping.", town, "an acquaintance the packet does not hold"),
                   "news asked for");
    check::is_true(clean("You'll find Tornar of the council there.", town), "the person, pointed to");
    check::is_true(!communicator::never_backed("when they met, which the speaker's memory does not say: yesterday"),
                   "a meeting's time depends on the packet");
}

TEST(being_talked_down_and_overhearing_are_the_memorys) {
    // Contract version 10. Talked down lately, this reply's calming words among them.
    const std::string steadied = "Good to see you. You steadied me, and I'll not forget it.";
    check::is_true(clean(steadied, packets::greeted_eased()), "talked down lately: backed");
    check::is_true(flags(steadied, packets::greeted_again(), "being talked down, which the speaker's memory does not hold"),
                   "met again, never talked down");
    check::is_true(clean("All right. That's steadied me a notch.", packets::calmed("afraid")), "this reply's calming words");
    // Overheard, only from someone who joined in unasked.
    const std::string overheard = "Pardon me, I couldn't help overhearing.";
    check::is_true(clean(overheard, packets::joined_surroundings()), "joined: backed");
    check::is_true(flags(overheard, packets::surroundings(), "overhearing the stranger, which the speaker's memory does not hold"),
                   "asked, not joined");
    check::is_true(clean("Only what I overhear with my head down.", packets::surroundings()), "hearing things in general claims nothing");
}

// How long a castellan has held their post (contract version 19), in C++'s words from the Self fact: the
// record's, not a time counted out. Not another count of years, and not for one whose record says none.
TEST(a_castellans_years_at_the_post_are_the_self_facts) {
    const FactPacket sworn = packets::about_a_castellan_with_a_past();
    check::is_true(clean("I command the garrison here, and have for ten years and more.", sworn), "the Self fact's years");
    check::is_true(flags("I command the garrison here, and have for twenty years and more.", sworn, "a distance or a time counted out"),
                   "not its years");
    check::is_true(flags("I command the garrison here, and have for ten years and more.", packets::about_them_without_a_past(),
                         "a distance or a time counted out"),
                   "the control: no years on record");
    check::is_true(clean("I've held it a few years.", packets::about_a_castellan_second_elsewhere()), "a few years, as the record has it");
    check::is_true(clean("Before this post I was second in command at the fortress above Mokdorl.", packets::about_a_castellan_second_elsewhere()),
                   "where they served: the Self fact's name");
}

// How the speaker's own work went (contract version 20): what it cost and the stranger's part are claims
// only the Account fact that holds them backs, in its own words.
TEST(how_the_work_went_is_the_accounts) {
    const FactPacket told = packets::with_account(packets::greeted_after("finished", "warm"));
    const Fact* account = communicator::account_of(told);
    check::is_true(account != nullptr, "the premise: an account");
    const std::string words = communicator::account_words(*account, false);
    check::is_true(clean("Welcome back. I heard how it went: " + words + ".", told), "the account, as it holds it: " + words);
    check::is_true(flags("Welcome back; you held when it counted.", packets::greeted_after("finished", "warm"),
                         "how the work went, which no account the packet holds says"),
                   "the control: no account");
    check::is_true(flags("Welcome back; you took a wound for it.", told, "how the work went, which no account the packet holds says"),
                   "a part the account does not hold");
    check::is_true(flags("I heard how it went: we lost too many.", told, "how the work went, which no account the packet holds says"),
                   "a cost the account does not hold");
}

// A question of theirs never answered (contract version 21): saying so needs the Recall fact that holds it.
TEST(a_question_never_answered_is_the_memorys) {
    check::is_true(clean("Hello again. You never did answer me, last time. Who were they to you?", packets::greeted_with_a_question()),
                   "backed by the Recall fact");
    check::is_true(flags("Hello again. You never did answer me, last time. Who were they to you?", packets::greeted_again(),
                         "a question of the speaker's never answered, which the speaker's memory does not hold"),
                   "the control: none held");
}
