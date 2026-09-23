// Raising a band on a world that already exists (nomad_api.hpp).
//
// What a caller -- the villain pass, a god, the orchestrator -- depends on: the band appears
// where it was asked, on the asker's account and keyed on the asker's age; the ground still
// decides what it is; every refusal names the field to change; the caller's world is
// untouched and the call is a function of its request; and nothing else in the band block
// loses its identity. Two reference defects the re-run write-backs carry are pinned too.
//
// Nodes are FOUND by asking the real call rather than re-deriving which gates hold here.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "fwg/nomad_api.hpp"
#include "fwg/request_error.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

// The entries of one kind in the world's one operation log, in call order.
template <typename Op>
std::vector<const Op*> ops_in(const World& w) {
    std::vector<const Op*> out;
    for (const HistoryOperation& entry : w.history.operations) {
        if (const auto* op = std::get_if<Op>(&entry)) out.push_back(op);
    }
    return out;
}

template <typename Op>
std::size_t count_ops(const World& w) {
    return ops_in<Op>(w).size();
}

template <typename Op>
const Op& first_op(const World& w) {
    const auto ops = ops_in<Op>(w);
    if (ops.empty()) check::fail("the log to hold an entry of this kind");
    return *ops.front();
}

template <typename Op>
const Op& last_op(const World& w) {
    const auto ops = ops_in<Op>(w);
    if (ops.empty()) check::fail("the log to hold an entry of this kind");
    return *ops.back();
}

World at(int phase, int size = 33) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = phase;
    return generate(resolve(r));
}

const World& genesis() {
    static const World w = at(16);
    return w;
}

std::string document(const World& w) {
    std::ostringstream out;
    write_world_json(out, w, /*pretty=*/false, /*include_timings=*/false);
    return out.str();
}

// A villain the world actually has: the seam this API exists for.
std::string a_villain(const World& w) {
    check::is_true(!w.villains.people.empty(), "a villain in the world, or the villain seam is untested");
    return w.villains.people.front().uid;
}

RaiseBandRequest asking(int node, const std::string& kind = "villain", std::optional<std::string> id = std::nullopt,
                        int age = 2) {
    RaiseBandRequest r;
    r.node = node;
    r.origin = NomadBandOrigin{kind, std::move(id), age};
    return r;
}

template <typename Call>
std::optional<RequestRefusal> refusal_of(Call&& call) {
    try {
        call();
    } catch (const RequestError& refused) {
        return refused.refusal();
    }
    return std::nullopt;
}

RequestRefusal refused(const World& w, const RaiseBandRequest& request, const std::string& what) {
    const std::optional<RequestRefusal> r = refusal_of([&] { (void)raise_band(w, request); });
    check::is_true(r.has_value(), what + " to be refused");
    check::is_true(!r->message.empty(), what + " to say why");
    return *r;
}

struct Raised {
    int node = -1;
    World world;
};

// The first node, in node order, the real call raises a band on. Every node passed over
// must have been refused by the ground.
Raised first_raised(const World& w, RaiseBandRequest request, int from = 0) {
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    for (std::size_t node = static_cast<std::size_t>(from); node < held->node_count(); ++node) {
        request.node = static_cast<int>(node);
        try {
            return {static_cast<int>(node), raise_band(w, request)};
        } catch (const RequestError& refused) {
            check::is_true(refused.refusal().refused_by_world, "node " + std::to_string(node) +
                                                                   " refused by the ground: " + refused.what());
        }
    }
    check::fail("no node in this world raises a band");
}

const Raised& villain_band() {
    static const Raised r = first_raised(genesis(), asking(0, "villain", a_villain(genesis())));
    return r;
}

const NomadBand* band_with(const World& w, const std::string& uid) {
    for (const NomadBand& b : w.nomads.groups) {
        if (b.uid == uid) return &b;
    }
    return nullptr;
}

std::string expected_uid(const World& before, int age, int node) {
    std::set<std::string> taken;
    for (const NomadBand& b : before.nomads.groups) taken.insert(b.uid);
    const std::string stem = "nomad-" + std::to_string(age) + "-" + std::to_string(node);
    std::string uid = stem;
    for (int suffix = 1; taken.count(uid) != 0; ++suffix) uid = stem + "-" + std::to_string(suffix);
    return uid;
}

}  // namespace

TEST(a_raised_band_appears_on_the_askers_account) {
    const World& before = genesis();
    const Raised& r = villain_band();
    const World& w = r.world;
    const std::string uid = expected_uid(before, 2, r.node);
    const NomadBand* band = band_with(w, uid);
    check::is_true(band != nullptr, uid + " in the band block");
    check::is_true(band->origin.kind == NomadOriginKind::Villain, "raised on a villain's account");
    check::equal(band->origin.id, a_villain(before), "naming the villain");
    check::equal(band->origin.age, 2, "in the age asked");
    check::equal(band->node, r.node, "at the node asked");
    check::is_true(band->parent_uid.empty(), "a placed band, not a daughter");
    check::is_true(!band->camps.empty() && band->camps.front().id == uid + "-camp-0", "with the camp it starts from");
    check::is_true(w.nomads.groups.size() > before.nomads.groups.size(), "the band set grew");

    // Placement is not re-run: every placed band keeps its uid, its ground and its kind.
    for (const NomadBand& old : before.nomads.groups) {
        if (!old.parent_uid.empty()) continue;
        const NomadBand* kept = band_with(w, old.uid);
        check::is_true(kept != nullptr, old.uid + " still walking");
        check::equal(kept->node, old.node, old.uid + " where it was");
        check::is_true(kept->classification == old.classification, old.uid + " what it was");
    }
    check::is_true(std::is_sorted(w.nomads.groups.begin(), w.nomads.groups.end(),
                                  [](const NomadBand& a, const NomadBand& b) { return a.uid < b.uid; }),
                   "the block in uid order");
    int counted = 0;
    for (const int c : w.nomads.counts) counted += c;
    check::equal(counted, static_cast<int>(w.nomads.groups.size()), "the counts recomputed over the band set");
    check::is_true(w.nomads.routes.has_value(), "the circuits rebuilt");
    check::equal(w.nomads.routes->routed + w.nomads.routes->stranded, static_cast<int>(w.nomads.groups.size()),
                 "every band routed or stranded");
    const bool indexed = std::any_of(w.encounters.entries.begin(), w.encounters.entries.end(),
                                     [&](const EncounterEntry& e) { return e.group_uid == uid; });
    check::is_true(indexed, "the encounter index rebuilt with it");
}

TEST(a_raised_band_is_recorded_as_the_reference_records_it) {
    const Raised& r = villain_band();
    check::equal(count_ops<NomadOperation>(r.world), std::size_t{1}, "one operation");
    const NomadOperation& op = first_op<NomadOperation>(r.world);
    check::equal(op.kind, std::string("nomad_request"), "the operation's kind");
    check::equal(op.api_version, 1, "api 1");
    check::equal(op.uid, expected_uid(genesis(), 2, r.node), "the band");
    check::equal(op.node, r.node, "the node");
    check::is_true(op.origin.kind == NomadOriginKind::Villain && op.origin.id == a_villain(genesis()) && op.origin.age == 2,
                   "the origin");
    check::is_true(!op.requested_classification.has_value(), "nothing asked for");
    check::equal(op.variation, std::int64_t{0}, "the default variation");
    const NomadBand* band = band_with(r.world, op.uid);
    check::is_true(band != nullptr && band->classification == op.classification, "the classification it drew");
}

TEST(raising_a_band_leaves_the_callers_world_alone_and_repeats_exactly) {
    const World& w = genesis();
    const std::string before = document(w);
    const World again = raise_band(w, asking(villain_band().node, "villain", a_villain(w)));
    check::is_true(document(w) == before, "the caller's world untouched");
    check::is_true((count_ops<NomadOperation>(w) == 0), "nothing recorded on it");
    check::is_true(document(again) == document(villain_band().world), "the same request, the same world");
}

TEST(the_ground_decides_and_a_request_is_honoured_only_where_it_holds) {
    const World& w = genesis();
    const int node = villain_band().node;
    std::vector<std::string> eligible;
    std::vector<std::string> refused_names;
    std::vector<RequestRefusal> refusals;
    for (int i = 0; i < kNomadClassCount; ++i) {
        const std::string name(to_string(static_cast<NomadClass>(i)));
        RaiseBandRequest r = asking(node, "god", std::string("god-of-test"));
        r.classification = name;
        try {
            const World out = raise_band(w, r);
            eligible.push_back(name);
            const NomadOperation& op = last_op<NomadOperation>(out);
            check::equal(std::string(to_string(op.classification)), name, name + " honoured where its gate holds");
            check::is_true(op.requested_classification == static_cast<NomadClass>(i), name + " recorded as asked");
            check::is_true(band_with(out, op.uid)->origin.kind == NomadOriginKind::God, "on the god's account");
        } catch (const RequestError& refused) {
            refused_names.push_back(name);
            refusals.push_back(refused.refusal());
        }
    }
    check::is_true(!eligible.empty(), "a node that raises somebody raises some classification");
    check::is_true(!refusals.empty(), "and not every one, or the refusal is untested here");
    for (const RequestRefusal& r : refusals) {
        check::equal(r.field, std::string("classification"), "the classification named");
        check::is_true(r.refused_by_world, "the ground's refusal, not the argument's");
        std::vector<std::string> offered;
        for (const RefusalValue& v : r.suggestion_candidates) offered.push_back(std::get<std::string>(v));
        std::vector<std::string> supported(eligible.begin(), eligible.begin() + static_cast<std::ptrdiff_t>(
                                                                                      std::min<std::size_t>(5, eligible.size())));
        check::is_true(offered == supported, "offering the classifications the node supports");
    }
}

TEST(a_second_band_at_the_same_node_takes_the_next_suffix) {
    const Raised& first = villain_band();
    const std::string stem = "nomad-2-" + std::to_string(first.node);
    const World second = raise_band(first.world, asking(first.node, "villain", a_villain(genesis())));
    const std::string uid = last_op<NomadOperation>(second).uid;
    check::equal(uid, expected_uid(first.world, 2, first.node), "suffixed past every band already there");
    check::is_true(uid.rfind(stem + "-", 0) == 0, "the stem kept, a suffix added: " + uid);
    check::equal(count_ops<NomadOperation>(second), std::size_t{2}, "both calls recorded, in order");
    // No cap: a spawner that stopped working partway through a long history would be wrong.
    World many = first.world;
    for (int i = 0; i < 3; ++i) many = raise_band(many, asking(first.node, "world"));
    check::equal(count_ops<NomadOperation>(many), std::size_t{4}, "raised again and again");

    // The uid is keyed on the ASKER's age, not the world's: a band raised on account of an age
    // the world is not in carries that age.
    const World elsewhen = raise_band(genesis(), asking(first.node, "god", std::string("god-of-test"), 7));
    check::equal(last_op<NomadOperation>(elsewhen).uid, "nomad-7-" + std::to_string(first.node),
                 "keyed on the age asked for");
    check::equal(band_with(elsewhen, "nomad-7-" + std::to_string(first.node))->origin.age, 7, "and carrying it");
}

TEST(a_refused_band_names_its_field) {
    const World& w = genesis();
    const int node = villain_band().node;
    const auto with = [&](auto change) {
        RaiseBandRequest r = asking(node, "villain", std::string("villain-x"));
        change(r);
        return r;
    };

    RequestRefusal r = refused(w, with([](RaiseBandRequest& q) { q.api_version = 2; }), "api 2");
    check::equal(r.field, std::string("api_version"), "api field");
    check::equal(std::string(r.code), std::string("UNSUPPORTED_VERSION"), "a protocol refusal");

    r = refused(at(12), with([](RaiseBandRequest&) {}), "a world with no bands");
    check::equal(r.field, std::string("world.nomads"), "the missing block");
    check::is_true(!r.retry, "fixed by generating further");

    r = refused(w, with([](RaiseBandRequest& q) { q.node.reset(); }), "no node");
    check::equal(r.field, std::string("node"), "node field");
    r = refused(w, with([](RaiseBandRequest& q) { q.node = -3; }), "a negative node");
    check::equal(r.field, std::string("node"), "node field");
    check::equal(std::string(r.kind), std::string("out_of_range"), "a bound");

    r = refused(w, with([](RaiseBandRequest& q) { q.origin.reset(); }), "no origin");
    check::equal(r.field, std::string("origin"), "origin field");
    // The reference's own words for a missing object, reproduced: its wrong_type calls
    // anything that is not an integer or text "a finite number".
    check::equal(r.message, std::string("origin must be a finite number; received None."), "the reference's words");
    r = refused(w, with([](RaiseBandRequest& q) { q.origin->kind = "demon"; }), "an unknown asker");
    check::equal(r.field, std::string("origin.kind"), "origin kind field");
    check::equal(r.choices.size(), std::size_t{3}, "world, god and villain offered");
    r = refused(w, with([](RaiseBandRequest& q) { q.origin->age.reset(); }), "no age");
    check::equal(r.field, std::string("origin.age"), "origin age field");
    r = refused(w, with([](RaiseBandRequest& q) { q.origin->age = -1; }), "a negative age");
    check::equal(r.field, std::string("origin.age"), "origin age field");
    check::equal(r.message, std::string("origin.age must be 0..None; received -1."), "the reference's words");

    // REFERENCE DEFECT, REPRODUCED: the catalogue refusal names the refused VALUE as its field.
    r = refused(w, with([](RaiseBandRequest& q) { q.classification = "pirates"; }), "an unknown classification");
    check::equal(r.field, std::string("pirates"), "the value named as the field");
    check::equal(std::string(r.kind), std::string("unknown_field"), "a catalogue refusal");
    r = refused(w, with([](RaiseBandRequest& q) { q.classification = "cultist"; }), "a near miss");
    check::is_true(!r.suggestion_candidates.empty() && r.suggestion_candidates.front() == RefusalValue(std::string("cultists")),
                   "did you mean cultists");

    r = refused(w, with([](RaiseBandRequest& q) { q.variation = -1; }), "a negative variation");
    check::equal(r.field, std::string("variation"), "variation field");
    r = refused(w, with([](RaiseBandRequest& q) { q.variation = 4294967296LL; }), "a variation past a seed");
    check::equal(r.field, std::string("variation"), "variation field");
    check::is_true(r.suggestion_value == RefusalValue(4294967295LL), "clamped to the largest seed");
    check::is_true(!r.refused_by_world, "an argument refusal is not the world's");
}

TEST(ground_that_will_not_raise_a_band_is_refused_naming_the_node) {
    const World& w = genesis();
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    RequestRefusal r = refused(w, asking(static_cast<int>(held->node_count())), "past the grid");
    check::equal(r.field, std::string("node"), "node field");
    check::is_true(r.refused_by_world, "the world's refusal");

    int water = -1;
    for (std::size_t i = 0; i < held->node_count() && water < 0; ++i) {
        if (w.layers.at("water_type").at(held->point_x[i], held->point_z[i]) != 0.0) water = static_cast<int>(i);
    }
    r = refused(w, asking(water), "the sea");
    check::equal(r.field, std::string("node"), "node field");
    check::is_true(r.message.find("cannot be raised on water") != std::string::npos, "saying why: " + r.message);

    // Land where no gate holds raises nobody -- a refusal naming the node, where the
    // reference raised a bare error with no field at all.
    std::optional<RequestRefusal> nobody;
    for (std::size_t i = 0; i < held->node_count() && !nobody; ++i) {
        if (w.layers.at("water_type").at(held->point_x[i], held->point_z[i]) != 0.0) continue;
        const std::optional<RequestRefusal> tried = refusal_of([&] { (void)raise_band(w, asking(static_cast<int>(i))); });
        if (tried && tried->message.find("raises nobody") != std::string::npos) nobody = tried;
    }
    check::is_true(nobody.has_value(), "some land that raises nobody");
    check::equal(nobody->field, std::string("node"), "node field");
    check::is_true(nobody->refused_by_world, "the ground's refusal");
}

TEST(every_cult_deepens_its_node_again_on_every_call) {
    // REFERENCE DEFECT, REPRODUCED (its own `limits` text records it): the write-backs run on
    // every request, and a cult's lift multiplies what its node already holds, so a node a
    // cult holds ratchets toward the ceiling across calls rather than settling.
    const World& w = genesis();
    RaiseBandRequest cult = asking(0, "villain", a_villain(w));
    cult.classification = "cultists";
    std::optional<Raised> raised;
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    for (std::size_t node = 0; node < held->node_count() && !raised; ++node) {
        cult.node = static_cast<int>(node);
        try {
            World out = raise_band(w, cult);
            const NomadBand* band = band_with(out, last_op<NomadOperation>(out).uid);
            if (!band->basis.ley_node_id.empty()) raised = Raised{static_cast<int>(node), std::move(out)};
        } catch (const RequestError&) {
        }
    }
    check::is_true(raised.has_value(), "a cult raised on a ley node");
    const NomadBand& band = *band_with(raised->world, last_op<NomadOperation>(raised->world).uid);
    const auto intensity = [&](const World& world) {
        for (const LeyNode& n : world.magic.networks.at(band.school).nodes) {
            if (n.id == band.basis.ley_node_id) return n.intensity;
        }
        check::fail("the cult's node in its network");
    };
    // Every cult on that node lifts it once per call, in turn.
    int holders = 0;
    for (const NomadBand& b : raised->world.nomads.groups) {
        if (b.classification == NomadClass::Cultists && b.school == band.school && b.basis.ley_node_id == band.basis.ley_node_id) {
            ++holders;
        }
    }
    const double once = intensity(raised->world);
    check::close(once, std::min(kNomadIntensityCeiling, intensity(w) * std::pow(1.0 + kNomadDevotionGain, holders)),
                 1e-9, "deepened once by the call that raised it");
    // Another band, far from anything sacred, deepens it again.
    const World next = raise_band(raised->world, asking(villain_band().node, "world"));
    check::close(intensity(next), std::min(kNomadIntensityCeiling, once * std::pow(1.0 + kNomadDevotionGain, holders)),
                 1e-9, "and again by the next call, which raised nobody there");
}

TEST(raid_pressure_is_added_again_on_every_call) {
    // REFERENCE DEFECT, REPRODUCED: the raid write-back adds onto the regional threat it
    // finds, and nothing re-reads the threat between calls, so a town beside raiders reads a
    // higher threat after every request, raised by the same bands.
    const World& before = genesis();
    const World& after = villain_band().world;
    std::size_t raided = 0;
    for (const ThreatReport& was : before.threat_assessments.cities) {
        if (was.nomad_contributors.empty()) continue;
        const auto now = std::find_if(after.threat_assessments.cities.begin(), after.threat_assessments.cities.end(),
                                      [&](const ThreatReport& t) { return t.city_uid == was.city_uid; });
        check::is_true(now != after.threat_assessments.cities.end() && now->nomad_pressure.has_value(),
                       was.city_uid + " still assessed and still raided");
        ++raided;
        check::close(now->regional_threat, std::min(1.0, was.regional_threat + *now->nomad_pressure), 2e-6,
                     was.city_uid + " raised again on top of the last raise");
        check::is_true(now->regional_threat > was.regional_threat || was.regional_threat == 1.0,
                       was.city_uid + " higher than before");
    }
    check::is_true(raided > 0, "some town beside raiders, or this tests nothing");
}
