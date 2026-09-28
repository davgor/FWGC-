// What a speaker says of themself (contract version 13): the words C++ says a Self fact and a Deed
// fact in, from their own fields, with the names the facts hold and no other. No model.
#include <string>

#include "check.hpp"
#include "them.hpp"

namespace {

using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;

// Eldsel the Castellan and Frossel of the Order, as seed 99 has them.
Fact castellan() {
    return {FactKind::Self,
            "hero-castellan-fortress-node-178",
            {{"role", "castellan"},
             {"at", "Iskmor (Age 1)"},
             {"known_as", "the Castellan"},
             {"fame", "notable"},
             {"home", "Iskmor (Age 1)"},
             {"stands", "garrison"},
             {"after", "hold"},
             {"after_what", "the fortress above Iskmor (Age 1)"},
             {"since", "this age"}}};
}

Fact champion() {
    return {FactKind::Self,
            "hero-champion-surface-city-0-153-human_cold",
            {{"role", "champion"},
             {"known_as", "of the Order"},
             {"stands", "refugee"},
             {"after", "kill"},
             {"after_what", "glacier dragons, the doom of Brenlin"},
             {"lost", "Brenlin"},
             {"lost_to", "dragon"},
             {"lost_when", "the last age"},
             {"since", "the last age"}}};
}

Fact deed(const std::string& kind, const std::string& part, const std::string& place, const std::string& when,
          const std::string& war = "") {
    Fact f{FactKind::Deed, "event-1", {{"deed", kind}, {"part", part}}};
    if (!place.empty()) f.fields.emplace_back("place", place);
    if (!war.empty()) f.fields.emplace_back("war", war);
    f.fields.emplace_back("when", when);
    return f;
}

}  // namespace

TEST(what_a_hero_is_after_how_they_stand_and_what_they_lost_are_said_from_their_fields) {
    using namespace communicator;
    check::equal(after_words(castellan()), std::string("hold the fortress above Iskmor"), "hold, the name as said");
    check::equal(after_words(champion()), std::string("kill the glacier dragons, the doom of Brenlin"), "kill, with its article");
    Fact pretender = champion();
    for (auto& [key, value] : pretender.fields) {
        if (key == "after") value = "retake";
        if (key == "after_what") value = "Eldkor";
    }
    check::equal(after_words(pretender), std::string("take back Eldkor"), "a proper name keeps no article");
    // Every claim a hero's card can make, in their words.
    for (const auto& [verb, said] : std::vector<std::pair<std::string, std::string>>{
             {"protect", "keep Eldkor safe"},
             {"exploit", "keep the trade of Eldkor turning"},
             {"convert", "bring Eldkor to the faith"}}) {
        Fact claim = pretender;
        for (auto& [key, value] : claim.fields)
            if (key == "after") value = verb;
        check::equal(after_words(claim), said, "the claim " + verb);
    }
    check::equal(standing_words(castellan()), std::string("command the garrison here"), "a garrison");
    check::equal(standing_words(champion()), std::string("am a refugee here"), "a refugee");
    check::equal(lost_words(champion()), std::string("lost Brenlin to a dragon in the last age"), "a city lost");
    check::equal(lost_words(castellan()), std::string(), "none lost, nothing said");
    check::equal(born_words(champion()), std::string("the last age"), "born in the last age");
    // A field the words do not know says nothing, rather than something made up.
    Fact odd = castellan();
    for (auto& [key, value] : odd.fields)
        if (key == "after" || key == "stands") value = "unheard_of";
    check::equal(after_words(odd), std::string(), "an unknown claim");
    check::equal(standing_words(odd), std::string(), "an unknown standing");
}

TEST(a_deed_is_said_by_its_kind_and_part_and_a_past_one_says_when) {
    using communicator::done_words;
    check::equal(done_words(deed("fortress", "keeper", "the fortress of Iskmor (Age 1)", "this age")),
                 std::string("command the fortress of Iskmor"), "a keeper, now");
    check::equal(done_words(deed("hamlet", "keeper", "the farmstead below Dunweg (Age 2)", "this age")),
                 std::string("speak for the farmstead below Dunweg"), "a reeve");
    check::equal(done_words(deed("ruin", "credited", "the ruins of Brenlin", "the last age")),
                 std::string("stood for Brenlin when it fell, in the last age"), "a city stood for, then");
    check::equal(done_words(deed("ruin", "heir", "Eldkor", "the last age")), std::string("am the heir of Eldkor"), "an heir");
    check::equal(done_words(deed("war", "credited", "", "this age", "international")), std::string("won a war between realms"),
                 "a war won, this age");
    check::equal(done_words(deed("war", "victim", "", "the last age", "civil")), std::string("lost a war among our own, in the last age"),
                 "a war lost, then");
    check::equal(done_words(deed("ruin", "credited", "", "the last age")), std::string(), "no place, no words");
    check::equal(done_words(deed("market", "keeper", "Wenweg", "this age")), std::string(), "a kind the words do not know");
}

TEST(ties_are_said_by_name_allies_first) {
    using communicator::ties_words;
    FactPacket p;
    p.facts.push_back({FactKind::Person, "hero-a", {{"name", "Iskgraf"}, {"role", "pretender"}, {"tie", "ally"}}});
    p.facts.push_back({FactKind::Person, "hero-b", {{"name", "Nerisk"}, {"role", "pretender"}, {"tie", "ally"}}});
    p.facts.push_back({FactKind::Person, "hero-c", {{"name", "Ulknord"}, {"role", "warlord"}, {"tie", "rival"}}});
    p.facts.push_back({FactKind::Person, "hero-d", {{"name", "Tamsin"}, {"role", "reeve"}}});
    check::equal(ties_words(p), std::string("Iskgraf and Nerisk stand with me; Ulknord is my rival"), "allies, then rivals; no one untied");
    FactPacket none;
    none.facts.push_back({FactKind::Person, "hero-d", {{"name", "Tamsin"}, {"role", "reeve"}}});
    check::equal(ties_words(none), std::string(), "nobody tied, nothing said");
}
