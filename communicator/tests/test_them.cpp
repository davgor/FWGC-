// What a speaker says of themself (contract version 13): the words C++ says a Self fact and a Deed
// fact in, from their own fields, with the names the facts hold and no other. No model.
#include <string>
#include <utility>
#include <vector>

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
    // Against whom (contract version 22).
    const auto against = [&](const std::string& part, const std::string& when, const std::string& city) {
        Fact f = deed("war", part, "", when, "international");
        f.fields.emplace_back("against", city);
        return f;
    };
    check::equal(done_words(against("credited", "this age", "Linbrig (Age 2)")), std::string("won a war between realms against Linbrig"),
                 "a war won, against whom");
    check::equal(done_words(against("victim", "the last age", "Linbrig (Age 1)")),
                 std::string("lost a war between realms to Linbrig, in the last age"), "a war lost, then, against whom");
    check::equal(done_words(against("credited", "ages ago", "the ruins of Brenlin")),
                 std::string("won a war between realms against Brenlin, ages ago"), "a city since fallen, by the name it had");
    check::equal(done_words(deed("ruin", "credited", "", "the last age")), std::string(), "no place, no words");
    check::equal(done_words(deed("market", "keeper", "Wenweg", "this age")), std::string(), "a kind the words do not know");
}

// Contract version 25: what someone else is known for, said of them: the verb agrees, and a war was with their
// neighbours or among their own, never ours; and how far word of it went.
TEST(what_someone_else_is_known_for_is_said_of_them) {
    using communicator::known_for_words;
    check::equal(known_for_words(deed("fortress", "keeper", "the fortress of Iskmor (Age 1)", "this age")),
                 std::string("commands the fortress of Iskmor"), "a keeper, of another");
    check::equal(known_for_words(deed("hamlet", "keeper", "the farmstead below Dunweg (Age 2)", "this age")),
                 std::string("speaks for the farmstead below Dunweg"), "a reeve");
    check::equal(known_for_words(deed("ruin", "heir", "Eldkor", "the last age")), std::string("is the heir of Eldkor"), "an heir");
    check::equal(known_for_words(deed("ruin", "credited", "the ruins of Brenlin", "the last age")),
                 std::string("stood for Brenlin when it fell, in the last age"), "the past as it was");
    check::equal(known_for_words(deed("war", "victim", "", "this age", "civil")), std::string("lost a war among their own"), "their own");
    check::equal(known_for_words(deed("war", "credited", "", "this age", "regional")), std::string("won a war with their neighbours"),
                 "their neighbours");
    check::equal(known_for_words(deed("market", "keeper", "Wenweg", "this age")), std::string(), "a kind the words do not know");
    const auto scoped = [&](const std::string& scope) {
        Fact f = deed("fortress", "keeper", "the fortress of Iskmor (Age 1)", "this age");
        f.fields.emplace_back("scope", scope);
        return communicator::far_words(f);
    };
    check::equal(scoped("local"), std::string(), "no further than where it was done: nothing");
    check::equal(scoped("regional"), std::string("all through these parts"), "a region");
    check::equal(scoped("realm"), std::string("all through the realm"), "the realm");
    check::equal(scoped("everywhere"), std::string(), "a scope the words do not know: nothing");
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
    // Who stands against them alone: the rivals, never an ally.
    using communicator::foes_words;
    check::equal(foes_words(p), std::string("Ulknord is my rival"), "the one rival");
    p.facts.push_back({FactKind::Person, "hero-e", {{"name", "Eldisk"}, {"role", "harbourmaster"}, {"tie", "rival"}}});
    check::equal(foes_words(p), std::string("Ulknord and Eldisk are my rivals"), "two rivals");
    FactPacket allies_only;
    allies_only.facts.push_back({FactKind::Person, "hero-a", {{"name", "Iskgraf"}, {"role", "pretender"}, {"tie", "ally"}}});
    check::equal(foes_words(allies_only), std::string(), "allies only: no rival said");
}

// A castellan's past (contract version 19): how long they have held their post, in bands a person would say
// ("the better part of ten years"), and where they served before it, and as what.
TEST(a_castellans_years_and_former_service_are_said_from_their_fields) {
    using communicator::held_words;
    using communicator::served_words;
    const auto with = [](std::vector<std::pair<std::string, std::string>> fields) {
        Fact f = castellan();
        for (auto& field : fields) f.fields.push_back(std::move(field));
        return f;
    };
    for (const auto& [years, said] : std::vector<std::pair<std::string, std::string>>{{"1", "a few years"},
                                                                                      {"5", "a few years"},
                                                                                      {"6", "the better part of ten years"},
                                                                                      {"9", "the better part of ten years"},
                                                                                      {"10", "ten years and more"},
                                                                                      {"14", "ten years and more"},
                                                                                      {"15", "near on twenty years"},
                                                                                      {"19", "near on twenty years"},
                                                                                      {"20", "twenty years and more"},
                                                                                      {"24", "twenty years and more"},
                                                                                      {"25", "near on thirty years"},
                                                                                      {"28", "near on thirty years"}})
        check::equal(held_words(with({{"held_years", years}})), said, years + " years");
    for (const std::string odd : {"0", "-3", "twelve", "", "12345"})
        check::equal(held_words(with({{"held_years", odd}})), std::string(), "not a count of years: " + odd);
    check::equal(held_words(castellan()), std::string(), "none on record, nothing said");
    check::equal(served_words(with({{"served_at", "the garrison of Iskmor (Age 1)"}, {"served_as", "soldier"}})),
                 std::string("served as a soldier in the garrison of Iskmor"), "a soldier of the garrison");
    check::equal(served_words(with({{"served_at", "the garrison of Iskmor (Age 1)"}, {"served_as", "sergeant"}})),
                 std::string("served as a sergeant in the garrison of Iskmor"), "a sergeant");
    check::equal(served_words(with({{"served_at", "the fortress above Mokdorl (Age 1)"}, {"served_as", "second"}})),
                 std::string("was second in command at the fortress above Mokdorl"), "second at another fortress");
    check::equal(served_words(with({{"served_at", "the garrison of Iskmor (Age 1)"}, {"served_as", "second"}})), std::string(),
                 "second, but not at a fortress: nothing made up");
    check::equal(served_words(with({{"served_at", "the garrison of Iskmor (Age 1)"}, {"served_as", "cook"}})), std::string(),
                 "a part the words do not know");
    check::equal(served_words(castellan()), std::string(), "none on record, nothing said");
}

TEST(the_sovereign_they_are_sworn_to_is_said_first_of_their_ties) {
    using communicator::ties_words;
    FactPacket p;
    p.facts.push_back({FactKind::Person, "hero-sovereign", {{"name", "Brenbrann"}, {"role", "sovereign"}, {"tie", "liege"}}});
    p.facts.push_back({FactKind::Person, "hero-c", {{"name", "Ulknord"}, {"role", "warlord"}, {"tie", "rival"}}});
    check::equal(ties_words(p), std::string("I'm sworn to Brenbrann, our sovereign; Ulknord is my rival"), "sworn, then the rival");
    FactPacket sworn_only;
    sworn_only.facts.push_back(p.facts.front());
    check::equal(ties_words(sworn_only), std::string("I'm sworn to Brenbrann, our sovereign"), "sworn alone");
    check::equal(communicator::foes_words(sworn_only), std::string(), "a liege is no rival");
}

// How the speaker's own work went (contract version 20), in C++'s words from the game's account: what came
// and how many, what it cost, the stranger's part; "at us" and "we lost" where the speaker was there.
TEST(an_account_of_the_work_is_said_from_its_fields) {
    using communicator::account_words;
    const auto account = [](std::vector<std::pair<std::string, std::string>> fields) {
        Fact f{FactKind::Account, "hook-1", {{"outcome", "finished"}}};
        for (auto& field : fields) f.fields.push_back(std::move(field));
        return f;
    };
    const Fact whole = account({{"came", "Abyssal krakens"}, {"came_as", "nest"}, {"many", "many"}, {"cost", "few"}, {"part", "held"}});
    check::equal(account_words(whole, true),
                 std::string("Abyssal krakens came at us, more than I'd have liked; we lost a few good people, and you held when it counted"),
                 "there: all of it");
    check::equal(account_words(whole, false),
                 std::string("Abyssal krakens came, more than I'd have liked; a few were lost, and you held when it counted"),
                 "not there: came, and were lost");
    for (const auto& [many, said] : std::vector<std::pair<std::string, std::string>>{
             {"one", "one of the Abyssal krakens came at us"},
             {"few", "Abyssal krakens came at us, a few of them"},
             {"horde", "Abyssal krakens came at us, more than anyone could count"},
             {"", "Abyssal krakens came at us"}})
        check::equal(account_words(account({{"came", "Abyssal krakens"}, {"came_as", "nest"}, {"many", many}}), true), said,
                     "a nest, how many: " + many);
    for (const auto& [many, said] : std::vector<std::pair<std::string, std::string>>{
             {"one", "Ulknord came at us alone"},
             {"few", "Ulknord came at us, with a few at their back"},
             {"many", "Ulknord came at us, with more at their back than I'd have liked"},
             {"horde", "Ulknord came at us, with more at their back than anyone could count"}})
        check::equal(account_words(account({{"came", "Ulknord"}, {"came_as", "person"}, {"many", many}}), true), said,
                     "a person, how many: " + many);
    for (const auto& [part, said] : std::vector<std::pair<std::string, std::string>>{{"fought", "you stood and fought"},
                                                                                     {"led", "you led the rest"},
                                                                                     {"turned", "you turned the fight"},
                                                                                     {"held", "you held when it counted"},
                                                                                     {"hurt", "you took a wound for it"}})
        check::equal(account_words(account({{"part", part}}), true), said, "the part alone: " + part);
    check::equal(account_words(account({{"cost", "none"}, {"part", "fought"}}), true), std::string("we lost no one, and you stood and fought"),
                 "what it cost, and the part");
    check::equal(account_words(account({{"cost", "many"}}), false), std::string("too many were lost"), "the cost alone, not there");
    check::equal(account_words(account({{"cost", "some"}, {"part", "won"}, {"many", "lots"}}), true), std::string(),
                 "words the account's words do not know: nothing made up");
    check::equal(account_words(account({}), true), std::string(), "an account that says nothing");
}
