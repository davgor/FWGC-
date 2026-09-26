// The truth checker: every claim a reply makes, held to its packet and its speaker's memory. Each
// kind of claim is shown backed by one packet and unbacked by another, so a check that stopped
// looking would pass the first and fail the second.
#include "communicator/truth.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "check.hpp"
#include "packets.hpp"
#include "text.hpp"

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
