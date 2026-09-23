// The world-level mutators: corrupt, cleanse, summon, depart.
//
// These test the glue -- that each call copies the world, writes the pass's result back, runs
// the rebuild it owes, redraws what the reference redraws and logs itself -- not the passes,
// which have their own suites (corruption, visitation).
#include "check.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <variant>

#include "fwg/beasties_stage.hpp"
#include "fwg/world_api.hpp"

using namespace fwg;

namespace {

// Two ages turned, villains seated at the last: a world a hidden god can reach into.
const World& finished() {
    static const World world = [] {
        Request r;
        r.seed = 42;
        r.size = 33;
        r.phase = 16;
        return generate(resolve(r));
    }();
    return world;
}

const GodRecord* god_of(const World& w, const std::string& id) {
    for (const GodRecord& g : w.religion.gods) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

template <typename Op>
const Op* last_op(const World& w) {
    if (w.history.operations.empty()) return nullptr;
    return std::get_if<Op>(&w.history.operations.back());
}

// Far past every hidden god's noticing point: whoever watches, bites.
CorruptWorldResult corrupted() {
    CorruptWorldRequest request;
    request.encounters = 1000;
    return corrupt(finished(), request);
}

// The first god the pantheon holds manifest, summoned onto the first standing city.
World summoned(std::string& god_id) {
    for (const GodRecord& g : finished().religion.gods) {
        if (g.status != "manifest") continue;
        SummonRequest request;
        request.god_id = g.id;
        request.target_city_uid = finished().settlements.sites.at(0).uid;
        try {
            World out = summon(finished(), request);
            god_id = g.id;
            return out;
        } catch (const VisitationError&) {
            continue;  // this god cannot come here; try the next
        }
    }
    check::fail("no manifest god could be summoned onto the first city");
}

}  // namespace

TEST(nothing_watching_leaves_the_world_as_it_was) {
    CorruptWorldRequest request;
    request.encounters = 0;  // below every noticing point
    const CorruptWorldResult result = corrupt(finished(), request);
    check::is_true(!result.report.acted, "nothing to act at zero encounters");
    check::is_true(result.report.watching.empty(), "and nothing watching");
    check::is_true(!result.report.reason.empty(), "saying why");
    check::equal(result.world.ruins.size(), finished().ruins.size(), "no ruin");
    check::equal(result.world.history.operations.size(), std::size_t{0}, "nothing logged");
    check::is_true(result.world.has_plans, "the plans untouched");
}

TEST(a_corruption_takes_a_villain_rebuilds_and_reveals_its_god) {
    const CorruptWorldResult result = corrupted();
    const World& w = result.world;
    check::is_true(result.report.acted, "a hidden god to bite at a thousand encounters");
    check::equal(w.corruptions.size(), std::size_t{1}, "one corruption record");
    check::equal(w.corruptions[0].villain_uid, result.report.villain_uid, "naming the villain it took");
    const GodRecord* god = god_of(w, result.report.god_id);
    check::is_true(god != nullptr && god->status == "walking", "the hidden god revealed walking after the resolve");
    // The rivals it roused stay Wild: the reveal runs AFTER the resolve, which would otherwise
    // recompute every known god's aspect and erase it.
    // Sixteen known gods have one face and are roused without being pushed; only a rival with
    // a Wild face can show the order.
    std::size_t pushed = 0;
    for (const std::string& rival : w.corruptions[0].roused) {
        const PantheonGod* known = pantheon_god(rival);
        if (known == nullptr || known->aspects.wild.empty()) continue;
        ++pushed;
        check::equal(god_of(w, rival)->aspect, std::string("wild"), rival + " roused to its Wild aspect");
    }
    check::is_true(pushed > 0, "some roused rival with a Wild face, or the order is untested");
    // The world was rebuilt, and the reference redraws no plan after a corruption.
    check::is_true(!w.has_plans && !w.has_world_scene, "the plans and the scene dropped");
    check::equal(w.threat_assessments.evaluated_after, std::string("corruption"), "the threat re-assessed");
    // The religion block carries the same record, as JSON.
    check::equal(w.religion.corruptions.size(), std::size_t{1}, "the religion block's copy");
    check::equal(w.religion.corruptions[0], corruption_record_json(w.corruptions[0]), "and it is this one");
    const auto* op = last_op<CorruptionOperation>(w);
    check::is_true(op != nullptr && op->god_id == result.report.god_id, "the corruption logged");
    // A corruption's ruins name it; the caller's world is untouched.
    for (std::size_t i = finished().ruins.size(); i < w.ruins.size(); ++i) {
        check::is_true(w.ruins[i].corruption == 0, w.ruins[i].id + " to name its corruption");
        check::is_true(w.ruins[i].cause == "rot_plague" || w.ruins[i].cause == "void_unmade", w.ruins[i].id + " cause");
    }
    check::equal(finished().corruptions.size(), std::size_t{0}, "the caller's world untouched");
}

TEST(a_full_cleanse_ends_the_corruption_and_puts_the_god_back_to_sleep) {
    const CorruptWorldResult bitten = corrupted();
    CleanseWorldRequest request;
    request.corruption = 0;
    request.power = 1.0;
    const CleanseWorldResult result = cleanse(bitten.world, request);
    const World& w = result.world;
    check::is_true(result.report.cleansed, "power 1.0 to drain every node in one tick");
    check::equal(result.report.kind, std::string("corruption"), "logged under the report's kind");
    check::is_true(w.corruptions[0].cleansed_age.has_value(), "the record closed");
    check::equal(god_of(w, bitten.report.god_id)->status, std::string("sleeping"), "the god asleep again");
    for (const std::string& id : result.report.removed) {
        const auto& nodes = w.magic.networks.at(w.corruptions[0].school).nodes;
        check::is_true(std::none_of(nodes.begin(), nodes.end(), [&](const LeyNode& n) { return n.id == id; }),
                       id + " gone from the field");
    }
    check::equal(w.religion.corruptions[0], corruption_record_json(w.corruptions[0]), "the religion copy follows");
    check::is_true(last_op<CleanseOperation>(w) != nullptr, "the cleanse logged");
}

TEST(a_god_revealed_by_corruption_is_opposed_through_its_corruption) {
    const CorruptWorldResult bitten = corrupted();
    CleanseWorldRequest request;
    request.god_id = bitten.report.god_id;
    try {
        (void)cleanse(bitten.world, request);
        check::fail("a corrupted god sent home by name");
    } catch (const CorruptionError& refused) {
        check::equal(refused.field(), std::string("target.god_id"), "refused naming the target");
    }
    CleanseWorldRequest both;
    both.corruption = 0;
    both.god_id = bitten.report.god_id;
    check::throws([&] { (void)cleanse(bitten.world, both); }, "two targets refused");
}

TEST(a_summoned_god_walks_leaves_a_theophany_and_the_plans_are_redrawn) {
    std::string god_id;
    const World w = summoned(god_id);
    check::equal(god_of(w, god_id)->status, std::string("walking"), "the god walks after the resolve");
    check::equal(w.visitations.size(), std::size_t{1}, "one visitation record");
    check::equal(w.religion.visitations.size(), std::size_t{1}, "and its copy in the religion block");
    check::equal(w.religion.visitations[0], visitation_record_json(w.visitations[0]), "the same record");
    check::is_true(w.has_plans && w.has_world_scene, "the plans and the scene redrawn");
    check::equal(w.city_plans.cities.size(), w.settlements.sites.size(), "a plan for every standing city");
    const auto* op = last_op<VisitationOperation>(w);
    check::is_true(op != nullptr && op->kind == "visitation", "the summons logged");
    bool theophany = false;
    for (const ReligionSite& s : w.religion.sites) theophany = theophany || s.kind == "theophany";
    check::is_true(theophany, "a theophany where it stood");
}

TEST(a_departed_god_goes_home_and_its_theophany_becomes_a_pilgrimage) {
    std::string god_id;
    const World walked = summoned(god_id);
    const World w = depart(walked, god_id);
    check::equal(god_of(w, god_id)->status, std::string("manifest"), "home again");
    check::is_true(w.visitations[0].departed_age.has_value(), "the record closed");
    bool pilgrimage = false;
    for (const ReligionSite& s : w.religion.sites) pilgrimage = pilgrimage || s.kind == "pilgrimage";
    check::is_true(pilgrimage, "the theophany carried forward as a pilgrimage");
    const auto* op = last_op<VisitationOperation>(w);
    check::is_true(op != nullptr && op->kind == "departure", "the departure logged");
    check::is_true(w.has_plans, "the plans redrawn");
    check::throws([&] { (void)depart(w, god_id); }, "a god that no longer walks cannot depart again");
}

TEST(a_cleanse_aimed_at_a_summoned_god_sends_it_home) {
    std::string god_id;
    const World walked = summoned(god_id);
    CleanseWorldRequest request;
    request.god_id = god_id;
    const CleanseWorldResult result = cleanse(walked, request);
    check::equal(god_of(result.world, god_id)->status, std::string("manifest"), "sent home");
    check::equal(result.report.kind, std::string("god"), "logged as the reference logs it");
    check::is_true(result.report.departed, "departed");
}

TEST(an_age_turn_sends_a_summoned_god_home) {
    std::string god_id;
    const World walked = summoned(god_id);
    const World later = advance_ages(walked, AgeAdvanceRequest{});
    check::equal(god_of(later, god_id)->status, std::string("manifest"), "the visitor went home with the age");
    check::is_true(later.visitations[0].departed_age.has_value(), "its record closed by the age");
    check::equal(later.history.operations.size(), std::size_t{2}, "the summons and the advance, in order");
    check::is_true(std::holds_alternative<VisitationOperation>(later.history.operations[0]), "summons first");
    check::is_true(std::holds_alternative<AgeOperation>(later.history.operations[1]), "then the advance");
}

TEST(an_age_cannot_yet_turn_over_ground_a_hidden_school_owns) {
    // PENDING THE OWNER'S RULING. The age lottery gives a city owned by a dominant school that
    // school's fall reason, and only the eight known schools have one; a corruption is what
    // lets a hidden school own ground. The reference raises a KeyError here, so the first age
    // turn after such a corruption fails. The port refuses by name instead. Either authoring
    // the four hidden reasons or keeping hidden schools out of the lottery closes it, and
    // this test is the one that must change when the ruling lands.
    const CorruptWorldResult bitten = corrupted();
    check::is_true(bitten.report.acted, "the corruption to act");
    try {
        (void)advance_ages(bitten.world, AgeAdvanceRequest{});
        check::fail("an age turned over hidden-school ground; the ruling has landed -- update this test");
    } catch (const std::out_of_range& refused) {
        check::is_true(std::string(refused.what()).find("no fall reason") != std::string::npos,
                       std::string("refused naming the missing reason: ") + refused.what());
    }
}

TEST(the_reveal_outlasts_the_pantheons_re_resolve) {
    // A school god's aspect is Wild when its network runs hot, which on a generated world it
    // nearly always does -- so the order of the reveal and the resolve is invisible there.
    // Quiet the rivals' networks first: the resolve alone would now make them Sovereign, and
    // only a reveal that runs AFTER it leaves them Wild.
    World quiet = finished();
    for (auto& [school, net] : quiet.magic.networks) {
        for (LeyNode& node : net.nodes) node.intensity = std::min(node.intensity, 0.1);
        for (LeyEdge& edge : net.edges) edge.intensity = std::min(edge.intensity, 0.1);
    }
    CorruptWorldRequest request;
    request.encounters = 1000;
    const CorruptWorldResult result = corrupt(quiet, request);
    check::is_true(result.report.acted, "the corruption to act on a quiet field");
    World resolved_alone = result.world;
    resolve_world_religion(resolved_alone);
    std::size_t shown = 0;
    for (const std::string& rival : result.world.corruptions.at(0).roused) {
        const PantheonGod* known = pantheon_god(rival);
        if (known == nullptr || known->aspects.wild.empty()) continue;
        if (god_of(resolved_alone, rival)->aspect == "wild") continue;  // cannot show the order
        ++shown;
        check::equal(god_of(result.world, rival)->aspect, std::string("wild"), rival + " kept Wild by the reveal");
    }
    check::is_true(shown > 0, "some rival the resolve alone would calm, or the order is untested");
}
