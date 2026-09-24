// What a player can name, learned from packets. No model: this is C++ deciding what words mean.
#include "communicator/known.hpp"

#include <string>
#include <vector>

#include "check.hpp"
#include "packets.hpp"

namespace {

using communicator::Kind;
using communicator::Known;
using communicator::Referent;

std::vector<std::string> labels(const Known& known, Kind kind) {
    std::vector<std::string> out;
    for (const Referent& r : known.nameable(kind)) out.push_back(r.label);
    return out;
}

std::string joined(const std::vector<std::string>& list) {
    std::string out;
    for (const std::string& s : list) out += "[" + s + "]";
    return out;
}

}  // namespace

TEST(a_packet_makes_its_places_people_and_work_nameable) {
    Known known;
    known.learn(packets::surroundings());
    known.learn(packets::work());
    check::equal(joined(labels(known, Kind::Place)),
                 joined({"Wenweg (Age 2)", "the ninth stone on the Linbrig road", "the hamlet of Wenweg (Age 2)", "the burnt works",
                         "the deeps under Linbrig", "the fortress of Wenweg (Age 2)", "Hakndum (Age 2)", "Nerstrann (Age 1)"}),
                 "places, as the director listed them, one per label");
    check::equal(joined(labels(known, Kind::Person)), joined({"Selbren"}), "the speaker is someone to ask about");
    check::equal(joined(labels(known, Kind::Offer)),
                 joined({"Selbren: Hold the fortress above Wenweg (Age 2) with me.",
                         "Nerbrann: Keep the sea lanes to the harbour of Wenweg (Age 2) open.", "Halbrann: Help me take back Linbrig.",
                         "Halbrann: Recover the reliquary of Linbrig from the ruins."}),
                 "work, named by its giver and what they say it is for");
    check::equal(known.resolve(Kind::Offer, "Halbrann: Help me take back Linbrig.").value_or(""), packets::kBrokeredOffer,
                 "a label back to its id");
    check::equal(known.resolve(Kind::Place, "the burnt works").value_or(""), std::string("keyloc-smelter_ruin-180"), "a place's id");
    check::is_true(labels(known, Kind::Held).empty(), "no work taken yet");
}

TEST(a_shared_label_means_the_nearest_in_a_packet_and_the_latest_across_packets) {
    Known known;
    known.learn(packets::surroundings());
    // Two hamlets of Wenweg in one packet: the director lists the nearer first.
    check::equal(known.resolve(Kind::Place, "the hamlet of Wenweg (Age 2)").value_or(""), std::string("hamlet-39"),
                 "the nearer hamlet, 3.1 km, over the one at 4.0 km");
    // Another speaker, elsewhere, names a different hamlet the same way: now that one is meant.
    known.learn(packets::surroundings_from_the_gate());
    check::equal(known.resolve(Kind::Place, "the hamlet of Wenweg (Age 2)").value_or(""), std::string("hamlet-38"),
                 "the hamlet the latest speaker named first");
    std::size_t hamlets = 0;
    for (const std::string& label : labels(known, Kind::Place)) hamlets += label == "the hamlet of Wenweg (Age 2)";
    check::equal(hamlets, std::size_t{1}, "one label offered, however many share it");
}

TEST(a_label_names_only_things_of_its_kind) {
    Known known;
    known.learn(packets::surroundings());
    check::is_true(!known.resolve(Kind::Person, "Wenweg (Age 2)"), "a place is not a person");
    check::is_true(!known.resolve(Kind::Place, "Selbren"), "a person is not a place");
    check::is_true(!known.resolve(Kind::Offer, "the burnt works"), "a place is not work");
    check::is_true(!known.resolve(Kind::Place, "Castle Grey"), "nothing the packets never named");
}

TEST(taken_work_is_held_and_abandoned_work_is_forgotten) {
    Known known;
    known.learn(packets::work());
    const std::string label = "Selbren: Hold the fortress above Wenweg (Age 2) with me.";
    known.learn(packets::accepted(packets::kOwnOffer));
    check::equal(joined(labels(known, Kind::Held)), joined({label}), "taken work, under the name it was offered by");
    check::is_true(!known.resolve(Kind::Offer, label), "and no longer on offer");
    check::equal(known.resolve(Kind::Held, label).value_or(""), packets::kOwnOffer, "held work resolves to its id");
    known.learn(packets::abandoned(packets::kOwnOffer));
    check::is_true(labels(known, Kind::Held).empty(), "abandoned work is not held");
}

TEST(what_cannot_be_named_is_not_offered) {
    Known known;
    // Work taken that no packet ever offered has no name a player could use.
    known.learn(packets::accepted("hook-never-offered-0"));
    check::is_true(labels(known, Kind::Held).empty(), "no name, nothing held to point at");
    // Dangers and reasons are said, never targeted; a place with neither a name nor a city is
    // not something a player can name.
    known.learn(packets::packet(packets::castellan(), orchestrator::ActKind::AskSurroundings, orchestrator::Outcome::Answered,
                                {{orchestrator::FactKind::Place, "hamlet-99", {{"kind", "hamlet"}, {"distance", "1.0 km"}}},
                                 {orchestrator::FactKind::Danger, "nest-griffons-178", {{"creature", "Griffons"}}},
                                 {orchestrator::FactKind::Reason, "", {{"reason", "the speaker is gone"}}}}));
    check::is_true(labels(known, Kind::Place).empty(), "a nameless place");
    check::equal(joined(labels(known, Kind::Person)), joined({"Selbren"}), "only the speaker");
}

TEST(the_oldest_are_forgotten_past_the_keep_limit) {
    Known known;
    const std::size_t packets_learned = Known::kKeptPerKind + 40;
    for (std::size_t i = 0; i < packets_learned; ++i)
        known.learn(packets::packet(packets::gate_captain(), orchestrator::ActKind::AskSurroundings, orchestrator::Outcome::Answered,
                                    {{orchestrator::FactKind::Place, "place-" + std::to_string(i),
                                      {{"kind", "city"}, {"name", "Town " + std::to_string(i)}}}}));
    check::equal(known.nameable(Kind::Place, 100000).size(), Known::kKeptPerKind, "no more kept than the limit");
    check::equal(known.nameable(Kind::Place).size(), Known::kMaxNameable, "and a few offered at a time");
    check::equal(known.nameable(Kind::Place).front().label, "Town " + std::to_string(packets_learned - 1), "the latest first");
    check::is_true(!known.resolve(Kind::Place, "Town 0"), "the oldest forgotten");
}

TEST(a_packet_of_another_contract_version_is_refused) {
    Known known;
    orchestrator::FactPacket later = packets::surroundings();
    later.contract_version = orchestrator::kContractVersion + 1;
    check::throws([&] { known.learn(later); }, "a packet this does not read");
}
