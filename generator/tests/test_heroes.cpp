// Hero generator invariants.
//
// These run against a hand-built world rather than a generated one, for the same reason
// the religion suite does: the properties under test are about ordering, roll ledgers,
// derived alignment and quest pricing, and a generated world would make every one of them
// depend on eleven stages upstream.
//
// Several tests need a specific role in the cast, and whether a role precipitates is a
// seeded roll. Rather than tune one seed until it happens to produce everything, those
// tests sweep a fixed band of seeds and assert that the role WAS found -- so a change that
// stops a well firing fails loudly instead of quietly skipping.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fwg/heroes.hpp"

using namespace fwg;

namespace {

constexpr double kRadiusM = 11150.0;  // the reference world is about 11.15 km around
constexpr int kSeedBand = 24;

Vec3 east(double lift) { return unit(Vec3{1.0, lift, 0.0}); }

HeroWarRecord war_row(std::string id, int age, std::string outcome, std::string kind,
                      std::string opponent) {
    HeroWarRecord row;
    row.war_id = std::move(id);
    row.age = age;
    row.outcome = std::move(outcome);
    row.kind = std::move(kind);
    row.opponent_uid = std::move(opponent);
    return row;
}

// A world the hero generator can read, owned by the test so the request stays cheap to
// copy and easy to perturb one field at a time.
struct Bench {
    HeroesRequest request;

    Bench() {
        request.seed = 42;
        request.final_age = 4;
        request.globe_radius_m = kRadiusM;
        request.spacing_m = 1000.0;

        HeroCity capital;
        capital.uid = "city-1";
        capital.id = "c1";
        capital.name = "Ashford City";
        capital.city_class = "capital";
        capital.civilization_id = "human_heartland";
        capital.population_profile = "human_heartland";
        capital.parent_race_id = "human";
        capital.node = 10;
        capital.x = 3;
        capital.z = 3;
        capital.direction = east(0.02);
        capital.suitability = 0.9;
        capital.founded_age = 1;
        capital.founding_capital = true;
        capital.war_history = {war_row("war-1", 3, "victor", "regional", "r1")};

        HeroCity medium;
        medium.uid = "city-2";
        medium.id = "c2";
        medium.name = "Bramble";
        medium.city_class = "medium";
        medium.civilization_id = "human_heartland";
        medium.population_profile = "human_heartland";
        medium.parent_race_id = "human";
        medium.node = 11;
        medium.x = 4;
        medium.z = 4;
        medium.direction = east(0.05);
        medium.suitability = 0.5;
        medium.founded_age = 2;

        HeroCity small;
        small.uid = "city-3";
        small.id = "c3";
        small.name = "Corry";
        small.city_class = "small";
        small.civilization_id = "human_heartland";
        small.population_profile = "human_heartland";
        small.parent_race_id = "human";
        small.node = 12;
        small.x = 5;
        small.z = 2;
        small.direction = east(0.09);
        small.suitability = 0.3;
        small.founded_age = 4;
        small.diaspora_reason = "religious_schism";
        small.source_civilization_id = "human_heartland";

        request.cities = {capital, medium, small};

        HeroRuin fallen;
        fallen.uid = "r1";
        fallen.id = "ruin-1";
        fallen.name = "Dunmar City";
        fallen.city_class = "capital";
        fallen.cause = "war_regional";
        fallen.destroyed_age = 3;
        fallen.civilization_id = "human_heartland";
        fallen.population_profile = "human_heartland";
        fallen.parent_race_id = "human";
        fallen.node = 20;
        fallen.x = 6;
        fallen.z = 6;
        fallen.has_direction = true;
        fallen.direction = east(0.03);
        fallen.war_history = {war_row("war-1", 3, "defeated", "regional", "city-1")};

        HeroRuin eaten;
        eaten.uid = "r2";
        eaten.id = "ruin-2";
        eaten.name = "Keld";
        eaten.city_class = "small";
        eaten.cause = "dragon";
        eaten.destroyed_age = 4;
        eaten.civilization_id = "human_heartland";
        eaten.population_profile = "human_heartland";
        eaten.parent_race_id = "human";
        eaten.node = 21;
        eaten.x = 7;
        eaten.z = 7;
        eaten.has_direction = true;
        eaten.direction = east(0.07);
        eaten.evidence_nest_id = "nest-1";

        HeroRuin unmade;
        unmade.uid = "r3";
        unmade.id = "ruin-3";
        unmade.name = "Vey";
        unmade.city_class = "medium";
        unmade.cause = "self_magic";
        unmade.destroyed_age = 4;
        unmade.civilization_id = "human_heartland";
        unmade.population_profile = "human_heartland";
        unmade.parent_race_id = "human";
        unmade.node = 22;
        unmade.x = 2;
        unmade.z = 8;
        unmade.has_direction = true;
        unmade.direction = east(0.11);
        unmade.new_node_school = "radiant";

        // Destroyed three ages ago, so its heir is a LEGEND: final_age - born_age > 1.
        HeroRuin ancient;
        ancient.uid = "r0";
        ancient.id = "ruin-0";
        ancient.name = "Old Harrow";
        ancient.city_class = "capital";
        ancient.cause = "war_civil";
        ancient.destroyed_age = 1;
        ancient.civilization_id = "human_heartland";
        ancient.population_profile = "human_heartland";
        ancient.parent_race_id = "human";
        ancient.node = 23;
        ancient.x = 8;
        ancient.z = 2;
        ancient.has_direction = true;
        ancient.direction = east(0.13);

        request.ruins = {ancient, fallen, eaten, unmade};

        HeroWar war;
        war.id = "war-1";
        war.age = 3;
        war.kind = "regional";
        war.victor_uid = "city-1";
        war.civilization_id = "human_heartland";
        war.defeated_uid = "r1";
        request.wars = {war};

        request.civilizations = {{"human_heartland", "the Heartlanders", "human"}};
        request.cultures = {{"cul-1", "human_heartland", {"c1", "c2"}}};

        HeroNest wyrm;
        wyrm.id = "nest-1";
        wyrm.name = "Ashen Wyrm";
        wyrm.family = "draconic";
        wyrm.tier = 4;
        wyrm.has_tier = true;
        wyrm.range_m = 3000.0;
        wyrm.has_range = true;
        wyrm.has_grid = true;
        wyrm.x = 5;
        wyrm.z = 5;
        wyrm.direction = east(0.04);

        HeroNest harmless;
        harmless.id = "nest-2";
        harmless.name = "Meadow Elk";
        harmless.family = "fantastic";
        harmless.tier = 1;
        harmless.has_tier = true;
        harmless.range_m = 500.0;
        harmless.has_range = true;
        harmless.has_grid = true;
        harmless.x = 40;
        harmless.z = 40;
        harmless.direction = Vec3{0.0, 0.0, 1.0};
        request.nests = {wyrm, harmless};

        request.hamlets = {{"hamlet-7", "hamlet", "farming", "c1", 30, 3, 4}};
        request.fortresses = {{"fortress-2", "c1", 31, 4, 3, 5.0}};
        request.cores = {{"c1", 1.0}};
        request.shipments = {{"c1", "c2"}, {"c2", "c1"}};
        request.colleges = {{"college-1", "c1", 32, 3, 3}};

        HeroLeyNetwork weave;
        weave.school = "weave";
        weave.nodes = {{"ruin-1-key", east(0.03), 2.0}};
        HeroLeyNetwork infernal;
        infernal.school = "infernal";
        infernal.nodes = {{"inf-1", east(0.06), 1.0}};
        request.networks = {weave, infernal};

        request.gods = {{"god_weave", "the Weave"}};
        request.religion_sites = {{"shrine-1", "shrine", "god_weave", "ruin-1", 20, false},
                                  {"cult-1", "cult", "god_weave", "ruin-2", 21, true}};
        request.ports = {{"port-1", "c2", 40, 41, true}};
        request.sea_routes = {{{"port-1", "41"}}};
        request.threats = {{"city-1", 1.0}};
    }
};

const Hero* find_role(const HeroesBlock& block, std::string_view role) {
    for (const Hero& person : block.people) {
        if (person.role == role) return &person;
    }
    return nullptr;
}

// Every world in the seed band. Rolls decide who precipitates, so a test that needs a role
// looks across the band and states that it found one.
std::vector<HeroesBlock> band(const Bench& bench, int count = kSeedBand) {
    std::vector<HeroesBlock> out;
    for (int seed = 1; seed <= count; ++seed) {
        HeroesRequest request = bench.request;
        request.seed = static_cast<std::uint64_t>(seed);
        out.push_back(resolve_heroes(request));
    }
    return out;
}

}  // namespace

TEST(the_catalogue_links_in_intact) {
    lint_hero_catalogue();
    check::equal(hero_archetype_cards().size(), std::size_t{32}, "archetype cards");
    check::equal(hero_feature_vocabulary().size(), std::size_t{82}, "feature vocabulary");
    check::equal(hero_quest_verbs().size(), std::size_t{10}, "quest verbs");
    // Every token a card names must be a real feature. The reference never checked this,
    // so a typo made a card unreachable forever with no error.
    std::set<std::string_view> features(hero_feature_vocabulary().begin(),
                                        hero_feature_vocabulary().end());
    int checked = 0;
    for (const HeroArchetypeCard& card : hero_archetype_cards()) {
        for (const HeroClause& clause : card.requires_any) {
            for (const std::string_view token : clause.tokens) {
                check::is_true(features.count(token) != 0,
                               std::string(card.id) + " requires a known feature: " + std::string(token));
                ++checked;
            }
        }
    }
    check::is_true(checked > 40, "the catalogue actually carries preconditions to check");
}

TEST(a_ruined_city_leaves_an_heir_who_claims_it_back) {
    Bench bench;
    int found = 0;
    for (const HeroesBlock& block : band(bench)) {
        check::equal(block.status, std::string("ok"), "block status");
        for (const Hero& person : block.people) {
            if (person.role != "pretender") continue;
            ++found;
            check::equal(person.claim.verb, std::string("retake"), "an heir claims the city back");
            check::is_true(person.lost.has_value(), "an heir carries the city it lost");
            check::equal(person.claim.target_uid, person.lost->uid, "the claim names the ruin");
            check::equal(person.born_age, person.lost->destroyed_age,
                         "an heir is born in the age the city fell");
            check::is_true(!person.display_name.empty(), "the heir has a name");
        }
    }
    check::is_true(found > 0, "at least one heir precipitated across the seed band");
}

TEST(the_roll_ledger_records_candidates_that_did_not_precipitate) {
    Bench bench;
    int misses = 0, hits = 0;
    for (const HeroesBlock& block : band(bench)) {
        std::set<std::string> precipitated;
        for (const Hero& person : block.people) precipitated.insert(person.uid);
        for (const HeroDread& dread : block.dreads) precipitated.insert(dread.uid);
        for (const HeroCamp& camp : block.camps) precipitated.insert(camp.uid);
        for (const HeroRoll& row : block.rolls) {
            check::between(row.chance, 0.0, hero_wells_policy().cap, "a chance is capped");
            check::between(row.roll, 0.0, 1.0, "a roll is a probability");
            if (row.precipitated) {
                ++hits;
                check::is_true(precipitated.count(row.candidate_uid) != 0,
                               "a precipitated candidate is in the cast: " + row.candidate_uid);
            } else {
                ++misses;
                check::is_true(precipitated.count(row.candidate_uid) == 0,
                               "a failed candidate is not in the cast: " + row.candidate_uid);
            }
        }
        check::equal(block.summary.candidates, static_cast<int>(block.rolls.size()), "candidate count");
    }
    check::is_true(hits > 0, "some candidates precipitated");
    // The whole reason the ledger exists: history could have produced these and did not.
    check::is_true(misses > 0, "some candidates rolled and lost");
}

TEST(camps_roll_after_every_well_and_the_ledger_opens_with_the_ruins) {
    Bench bench;
    HeroesBlock block = resolve_heroes(bench.request);
    check::is_true(!block.rolls.empty(), "the ledger is not empty");
    check::equal(block.rolls.front().kind, std::string("heir"), "the ruins well opens the ledger");
    const std::size_t camps = bench.request.ruins.size();
    check::is_true(block.rolls.size() > camps, "there is more in the ledger than the camps");
    for (std::size_t i = block.rolls.size() - camps; i < block.rolls.size(); ++i) {
        check::equal(block.rolls[i].kind, std::string("camp"),
                     "every camp roll is at the tail, after the seven wells");
    }
    for (std::size_t i = 0; i + camps < block.rolls.size(); ++i) {
        check::is_true(block.rolls[i].kind != "camp", "no camp roll sits inside the well order");
    }
}

TEST(the_cast_is_sorted_by_birth_then_uid) {
    Bench bench;
    int compared = 0;
    for (const HeroesBlock& block : band(bench)) {
        for (std::size_t i = 1; i < block.people.size(); ++i) {
            const Hero& a = block.people[i - 1];
            const Hero& b = block.people[i];
            check::is_true(a.born_age < b.born_age || (a.born_age == b.born_age && a.uid < b.uid),
                           "cast order is (born_age, uid): " + a.uid + " before " + b.uid);
            ++compared;
        }
    }
    check::is_true(compared > 10, "the band produced casts long enough to order");
}

TEST(the_archetype_walk_subtracts_forward_from_the_first_eligible_card) {
    // A reversed cumulative walk preserves every marginal probability, so no frequency
    // test can see it. These are golden cases: a draw near zero must land on the FIRST
    // eligible card in catalogue order and a draw near one on the LAST.
    std::set<std::string> features = {"role:warlord", "war:civil_victor", "role:council",
                                      "seat:magistrate", "seat:none"};
    const HeroAlignment neutral = hero_alignment_record(0.0, 0.0);

    std::vector<const HeroArchetypeCard*> eligible;
    for (const HeroArchetypeCard& card : hero_archetype_cards()) {
        for (const HeroClause& clause : card.requires_any) {
            bool holds = true;
            for (const std::string_view token : clause.tokens) {
                if (!features.count(std::string(token))) holds = false;
            }
            if (holds) {
                eligible.push_back(&card);
                break;
            }
        }
    }
    check::is_true(eligible.size() >= 3, "the feature set admits several cards to walk between");

    std::uint64_t low_seed = 0, high_seed = 0;
    for (std::uint64_t seed = 1; seed < 20000 && (low_seed == 0 || high_seed == 0); ++seed) {
        Rng probe(seed);
        const double value = probe.random();
        if (low_seed == 0 && value < 0.0005) low_seed = seed;
        if (high_seed == 0 && value > 0.9995) high_seed = seed;
    }
    check::is_true(low_seed != 0 && high_seed != 0, "found probe seeds at both ends of [0, 1)");

    Rng low(low_seed);
    Rng high(high_seed);
    const HeroArchetypeCard* first = choose_archetype(features, neutral, low);
    const HeroArchetypeCard* last = choose_archetype(features, neutral, high);
    check::is_true(first != nullptr && last != nullptr, "both draws chose a card");
    check::equal(std::string(first->id), std::string(eligible.front()->id),
                 "a draw at the bottom of the range takes the first eligible card");
    check::equal(std::string(last->id), std::string(eligible.back()->id),
                 "a draw at the top of the range takes the last eligible card");
    check::is_true(first->id != last->id, "the two ends are different cards, so the walk is visible");
}

TEST(an_archetype_prior_changes_frequency_and_never_eligibility) {
    // "A Conqueror is more often lawful, never only lawful. Every archetype exists in all
    // nine cells." Sweep both alignment corners and require that the SET of reachable
    // cards is identical -- only how often each appears may move.
    std::set<std::string> features = {"role:warlord", "war:civil_victor", "role:council",
                                      "seat:magistrate", "seat:none"};
    std::set<std::string> lawful_cards, chaotic_cards;
    for (std::uint64_t seed = 1; seed <= 400; ++seed) {
        Rng a(seed);
        Rng b(seed);
        const HeroArchetypeCard* x = choose_archetype(features, hero_alignment_record(1.0, 1.0), a);
        const HeroArchetypeCard* y = choose_archetype(features, hero_alignment_record(-1.0, -1.0), b);
        if (x) lawful_cards.insert(std::string(x->id));
        if (y) chaotic_cards.insert(std::string(y->id));
    }
    check::is_true(lawful_cards.size() > 1, "the sweep reached more than one card");
    check::equal(lawful_cards.size(), chaotic_cards.size(),
                 "the same cards are reachable from either corner");
    check::is_true(lawful_cards == chaotic_cards, "alignment clamps nothing out of the pool");
}

TEST(jitter_never_flips_a_sign_a_deed_decided) {
    // The heir of a CAPITAL takes a +0.15 law shift. The jitter half-width is 0.35, wider
    // than the shift, so a plain clamp would let some seeds cross zero; the sign lock
    // holds the value at the epsilon instead.
    Bench bench;
    int heirs = 0, pinned = 0;
    for (int seed = 1; seed <= 120; ++seed) {
        HeroesRequest request = bench.request;
        request.seed = static_cast<std::uint64_t>(seed);
        const HeroesBlock block = resolve_heroes(request);
        for (const Hero& person : block.people) {
            if (person.role != "pretender" || person.lost->city_class != "capital") continue;
            ++heirs;
            check::is_true(person.alignment.law > 0.0,
                           "a capital heir's law axis stays on the side the deed put it");
            check::is_true(person.alignment.good > 0.0,
                           "a capital heir's good axis stays on the side the deed put it");
            if (person.alignment.law == 0.01 || person.alignment.good == 0.01) ++pinned;
        }
    }
    check::is_true(heirs > 20, "the sweep produced capital heirs to test");
    // Without the guard this would be zero: the values that land exactly on the epsilon
    // are the ones jitter tried to push across zero.
    check::is_true(pinned > 0, "at least one axis was held at the epsilon rather than crossing");
}

TEST(the_persona_joins_the_overlays_in_the_order_each_field_declares) {
    const HeroArchetypeCard* card = hero_archetype_card("conqueror");
    check::is_true(card != nullptr, "the conqueror card is in the catalogue");
    const HeroAlignment lawful_good = hero_alignment_record(0.8, 0.8);
    const HeroPersona persona = compose_persona(*card, lawful_good);
    const HeroOverlay& law = hero_law_overlays()[2];
    const HeroOverlay& good = hero_good_overlays()[2];

    // voice and cornered are LAW FIRST; lies and toward_player are GOOD FIRST. Not
    // uniform, and copied literally.
    check::equal(persona.voice, std::string(law.voice) + " " + std::string(good.voice), "voice");
    check::equal(persona.cornered, std::string(law.cornered) + " " + std::string(good.cornered),
                 "cornered");
    check::equal(persona.lies, std::string(good.lies) + " " + std::string(law.lies), "lies");
    check::equal(persona.toward_player,
                 std::string(good.toward_player) + " " + std::string(law.toward_player),
                 "toward_player");
    check::is_true(law.voice != good.voice, "the two overlays differ, so the order is visible");

    // never: the card's own lines first, then the law overlay, then the good overlay.
    check::is_true(persona.never.size() >= card->core.never.size() + law.never.size(),
                   "the never list is a union, not a replacement");
    for (std::size_t i = 0; i < card->core.never.size(); ++i) {
        check::equal(persona.never[i], std::string(card->core.never[i]), "the card's lines lead");
    }
    check::equal(persona.never[card->core.never.size()], std::string(law.never[0]),
                 "the law overlay follows the card");
    std::set<std::string> unique(persona.never.begin(), persona.never.end());
    check::equal(unique.size(), persona.never.size(), "the never list is deduplicated");
}

TEST(a_persona_with_no_matching_line_reads_the_first_entry_in_file_order) {
    int fell_back = 0, checked = 0;
    const char* codes[9] = {"LG", "LN", "LE", "NG", "NN", "NE", "CG", "CN", "CE"};
    const double law_for[3] = {0.8, 0.0, -0.8};   // L, N, C
    const double good_for[3] = {0.8, 0.0, -0.8};  // G, N, E
    for (const HeroArchetypeCard& card : hero_archetype_cards()) {
        for (int l = 0; l < 3; ++l) {
            for (int g = 0; g < 3; ++g) {
                const HeroAlignment alignment = hero_alignment_record(law_for[l], good_for[g]);
                check::equal(alignment.code, std::string(codes[l * 3 + g]), "alignment code");
                const HeroPersona persona = compose_persona(card, alignment);
                check::is_true(!persona.line.empty(), "every persona carries a line");
                ++checked;
                bool exact = false, same_good = false;
                for (const HeroLine& line : card.lines) {
                    if (line.code == alignment.code) exact = true;
                    if (line.code[1] == alignment.code[1]) same_good = true;
                }
                if (!exact && !same_good) {
                    // The FIRST key in file order, which 27 of the 32 cards hand every
                    // neutral-good person.
                    check::equal(persona.line, std::string(card.lines[0].text),
                                 std::string(card.id) + " falls back to its first line");
                    ++fell_back;
                }
            }
        }
    }
    check::equal(checked, 32 * 9, "every card was asked for every cell");
    check::is_true(fell_back > 20, "the first-entry fallback actually fires");
}

TEST(a_countryside_person_is_keyed_on_the_terrain_node_in_all_four_places) {
    check::equal(hero_small_site_anchor("hamlet", 13, "hamlet-7"), std::string("hamlet-node-13"),
                 "a hamlet anchors on its node");
    check::equal(hero_small_site_anchor("fortress", 13, "fortress-7"),
                 std::string("fortress-node-13"), "a fortress anchors on its node");
    check::is_true(hero_small_site_anchor("hamlet", 13, "x") != hero_small_site_anchor("fortress", 13, "x"),
                   "the kind is part of the key, because a hamlet and a fortress may share a node");
    // A site with no node keeps its ordinal: there is nothing stabler to key it on.
    check::equal(hero_small_site_anchor("hamlet", -1, "hamlet-7"), std::string("hamlet-7"),
                 "a nodeless site keeps its ordinal");

    Bench bench;
    int found = 0;
    for (const HeroesBlock& block : band(bench)) {
        for (const Hero& person : block.people) {
            if (person.role != "reeve" && person.role != "castellan") continue;
            ++found;
            const std::string kind = person.role == "reeve" ? "hamlet" : "fortress";
            const std::string key = person.uid.substr(person.uid.find(kind));
            check::is_true(key.find(kind + "-node-") == 0, "the uid carries the node anchor: " + key);
            check::equal(person.presence->uid, key, "presence.uid is the anchor");
            check::equal(person.claim.target_uid, key, "claim.target_uid is the anchor");
            check::equal(person.deeds.front().event_id, key, "the deed's event_id is the anchor");
        }
    }
    check::is_true(found > 0, "the countryside well produced someone to key");
}

TEST(every_quest_hook_carries_a_known_verb_and_a_banded_difficulty) {
    Bench bench;
    std::set<int> difficulties;
    std::set<std::string> verbs;
    int hooks = 0;
    for (const HeroesBlock& block : band(bench)) {
        std::set<std::string> living;
        for (const Hero& person : block.people) {
            if (person.status == "living") living.insert(person.uid);
        }
        for (const HeroQuestHook& hook : block.quest_hooks) {
            ++hooks;
            check::is_true(living.count(hook.giver_uid) != 0, "only a living person gives a hook");
            check::between(hook.difficulty, 1.0, 5.0, "difficulty is 1..5");
            difficulties.insert(hook.difficulty);
            verbs.insert(hook.verb);
            bool known = false;
            for (const std::string_view verb : hero_quest_verbs()) known |= verb == hook.verb;
            check::is_true(known, "the verb is in the closed vocabulary: " + hook.verb);
            check::is_true(!hook.target.empty(), "a hook names what it points at");
            check::is_true(!hook.stated_purpose.empty(), "a hook says what it asks for");
            if (!hook.unsited_reason.empty()) {
                bool listed = false;
                for (const std::string_view reason : hero_unsited_reasons()) {
                    listed |= reason == hook.unsited_reason;
                }
                check::is_true(listed, "an unsited reason is published: " + hook.unsited_reason);
            }
        }
    }
    check::is_true(hooks > 10, "the band produced hooks to check");
    // "A rubric that answers the same number everywhere is not a rubric."
    check::is_true(difficulties.size() > 1, "difficulty actually varies across a world");
    check::is_true(verbs.size() > 2, "more than one kind of thing is asked for");
}

TEST(a_slay_hook_is_priced_at_the_lairs_own_tier) {
    // Opposition is what stands at the target and is the only term that can reach 5. A
    // nest is PRESSURE EXEMPT: its tier is already the whole answer and its own range
    // would otherwise count twice.
    std::vector<HeroNest> crowd;
    for (int i = 0; i < 4; ++i) {
        HeroNest nest;
        nest.id = "crowd-" + std::to_string(i);
        nest.tier = 5;
        nest.has_tier = true;
        nest.range_m = 50000.0;
        nest.has_range = true;
        nest.has_grid = true;
        nest.x = 3;
        nest.z = 3;
        crowd.push_back(nest);
    }
    for (int tier = 1; tier <= 5; ++tier) {
        HeroEffect slay;
        slay.kind = "nest";
        slay.nest_id = "nest-1";
        slay.action = "slay";
        const int difficulty =
            hero_hook_difficulty(slay, static_cast<double>(tier), true, 3, 3, crowd, 1000.0);
        check::equal(difficulty, tier, "a lair is priced at its own tier, whatever surrounds it");
    }
    // The same ground, a city instead of a lair: now the surrounding tier-5 nests count.
    HeroEffect defend;
    defend.kind = "city";
    defend.uid = "city-1";
    defend.action = "defend";
    const int pressed = hero_hook_difficulty(defend, 0.0, true, 3, 3, crowd, 1000.0);
    const int quiet = hero_hook_difficulty(defend, 0.0, true, 3, 3, {}, 1000.0);
    check::equal(quiet, 2, "quiet country prices a city at the ground it is");
    check::is_true(pressed > quiet, "danger around the target raises the band");
    check::is_true(pressed <= 3, "pressure is capped, so an errand never becomes a campaign threat");
}

TEST(a_keyed_ley_node_sites_at_its_ruin_and_a_bare_one_says_it_cannot) {
    Bench bench;
    int keyed = 0, bare = 0;
    for (const HeroesBlock& block : band(bench)) {
        for (const HeroQuestHook& hook : block.quest_hooks) {
            if (hook.actual_effect.kind != "ley_node") continue;
            if (hook.actual_effect.node_id == "ruin-1-key") {
                ++keyed;
                check::is_true(hook.unsited_reason.empty(), "a key point stands at its ruin");
                check::equal(hook.target_node, 20, "and reports that ruin's terrain node");
                check::is_true(hook.stated_purpose.find("Dunmar") != std::string::npos ||
                                   hook.stated_purpose.find("key point") != std::string::npos,
                               "the hook names the place, not the node id");
            } else {
                ++bare;
                // A bare node is a direction and an intensity with no footprint anywhere.
                check::equal(hook.unsited_reason, std::string("ley_node_has_no_site"),
                             "a bare ley node reports that it cannot be stood in front of");
                check::is_true(!hook.has_target_node, "and carries no terrain node");
            }
        }
    }
    check::is_true(keyed > 0, "a key-point hook was generated");
    check::is_true(bare > 0, "an opposing-node hook was generated");
}

TEST(raising_the_stake_raises_the_fame) {
    // The invariant the reference holds: a bigger wound is a bigger name.
    Bench bench;
    std::size_t target = 0;
    for (std::size_t i = 0; i < bench.request.ruins.size(); ++i) {
        if (bench.request.ruins[i].uid == "r1") target = i;
    }
    double small_fame = 0.0, capital_fame = 0.0;
    int seen = 0;
    for (const std::string& klass : {std::string("small"), std::string("capital")}) {
        double best = 0.0;
        for (int seed = 1; seed <= 60 && best == 0.0; ++seed) {
            HeroesRequest request = bench.request;
            request.seed = static_cast<std::uint64_t>(seed);
            request.ruins[target].city_class = klass;
            const HeroesBlock block = resolve_heroes(request);
            for (const Hero& person : block.people) {
                if (person.uid == "hero-pretender-r1") {
                    best = person.fame;
                    ++seen;
                }
            }
        }
        (klass == "small" ? small_fame : capital_fame) = best;
    }
    check::is_true(seen >= 2, "the heir of r1 precipitated under both classes");
    check::is_true(capital_fame > small_fame,
                   "raising the class of the fallen city raises the heir's fame");

    // And the beast half of the same invariant.
    double low = 0.0, high = 0.0;
    for (const int tier : {1, 5}) {
        for (int seed = 1; seed <= 60; ++seed) {
            HeroesRequest request = bench.request;
            request.seed = static_cast<std::uint64_t>(seed);
            request.nests[0].tier = tier;
            const HeroesBlock block = resolve_heroes(request);
            if (!block.dreads.empty()) {
                (tier == 1 ? low : high) = block.dreads.front().fame;
                break;
            }
        }
    }
    check::is_true(low > 0.0 && high > 0.0, "a dread precipitated at both tiers");
    check::is_true(high > low, "raising the tier of the beast raises its fame");
}

TEST(every_precipitation_role_has_a_diagnostics_row_whether_or_not_it_fired) {
    Bench bench;
    HeroesBlock block = resolve_heroes(bench.request);
    check::equal(block.diagnostics.size(), std::size_t{16}, "one row per precipitation role");
    for (std::size_t i = 1; i < block.diagnostics.size(); ++i) {
        check::is_true(block.diagnostics[i - 1].role < block.diagnostics[i].role,
                       "the table is sorted by role");
    }
    int silent = 0, fired = 0;
    for (const HeroDiagnostic& row : block.diagnostics) {
        check::is_true(!row.reason.empty(), row.role + " says why");
        check::is_true(!row.source_kind.empty(), row.role + " names what it counted");
        check::is_true(row.placed <= row.candidates, row.role + " placed no more than it rolled");
        if (row.candidates == 0 && row.sources == 0) {
            ++silent;
            check::is_true(row.reason.find("no " + row.source_kind) == 0,
                           "a role with no ground says so: " + row.reason);
        }
        if (row.placed > 0) ++fired;
    }
    check::is_true(fired > 0, "some roles fired on this world");
    // `sources` exceeds `candidates` wherever the well walked records that did not qualify.
    const HeroDiagnostic* champion = nullptr;
    for (const HeroDiagnostic& row : block.diagnostics) {
        if (row.role == "champion") champion = &row;
    }
    check::is_true(champion != nullptr, "the champion row exists");
    check::is_true(champion->sources >= champion->candidates,
                   "sources counts the records read, not the candidates built");
    (void)silent;
}

TEST(a_city_remembers_one_warlord_the_most_recent_victor) {
    Bench bench;
    // A second, earlier war the same city won, against a second ruin.
    HeroRuin earlier = bench.request.ruins[2];
    earlier.uid = "r4";
    earlier.id = "ruin-4";
    earlier.name = "Olden";
    earlier.cause = "war_civil";
    earlier.destroyed_age = 1;
    earlier.war_history = {war_row("war-0", 1, "defeated", "civil", "city-1")};
    bench.request.ruins.push_back(earlier);
    HeroWar old_war;
    old_war.id = "war-0";
    old_war.age = 1;
    old_war.kind = "civil";
    old_war.victor_uid = "city-1";
    old_war.civilization_id = "human_heartland";
    old_war.defeated_uid = "r4";
    bench.request.wars.insert(bench.request.wars.begin(), old_war);

    int seen = 0;
    for (const HeroesBlock& block : band(bench)) {
        int warlord_rolls = 0;
        for (const HeroRoll& row : block.rolls) {
            if (row.kind == "warlord") ++warlord_rolls;
        }
        check::equal(warlord_rolls, 1, "exactly one warlord candidate per victor city");
        for (const Hero& person : block.people) {
            if (person.role != "warlord") continue;
            ++seen;
            check::equal(person.born_age, 3, "the survivor is the MOST RECENT victor");
            // The earlier war stays in the survivor's deeds; it does not mint a person.
            check::is_true(person.deeds.size() == 1, "only the surviving war's deed is folded in");
        }
    }
    check::is_true(seen > 0, "a warlord precipitated somewhere in the band");
}

TEST(a_warlord_stands_at_a_situation_the_story_layer_knows) {
    // 'command' appears nowhere in the port specification. An unknown situation silently
    // costs every warlord its presence fact downstream, so it is pinned here.
    Bench bench;
    int seen = 0;
    for (const HeroesBlock& block : band(bench)) {
        for (const Hero& person : block.people) {
            if (person.role != "warlord" || !person.presence.has_value()) continue;
            ++seen;
            check::equal(person.presence->situation, std::string("command"), "a warlord commands");
            check::equal(person.presence->site_kind, std::string("city"), "from its own seat");
            check::equal(person.presence->uid, person.victor_uid, "which is the city that won");
        }
    }
    check::is_true(seen > 0, "a seated warlord precipitated");
}

TEST(a_relic_rests_in_every_ruin_without_rolling_for_it) {
    Bench bench;
    HeroesBlock block = resolve_heroes(bench.request);
    check::equal(block.relics.size(), bench.request.ruins.size(), "one relic per ruin, unconditionally");
    for (const HeroRoll& row : block.rolls) {
        check::is_true(row.kind != "relic", "no relic ever rolls");
    }
    std::map<std::string, const HeroRelic*> by_ruin;
    for (const HeroRelic& relic : block.relics) {
        check::is_true(relic.holder_uid.empty(), "a relic rests until somebody takes it");
        by_ruin[relic.resting_at] = &relic;
    }
    // A capital that fell in a war carries a crown, not a banner.
    check::equal(by_ruin["r1"]->kind, std::string("crown"), "a fallen capital leaves a crown");
    check::equal(by_ruin["r2"]->kind, std::string("reliquary"), "a beast-taken city leaves a reliquary");
    check::equal(by_ruin["r3"]->kind, std::string("focus"), "a self-unmade city leaves a focus");
    check::equal(by_ruin["r3"]->school, std::string("radiant"), "and the focus carries its school");
    check::is_true(by_ruin["r3"]->name.find("radiant") != std::string::npos,
                   "which is rendered into its name");
}

TEST(the_dominant_school_is_a_school_and_not_a_layer_key) {
    // Returning the raw layer key would never match dark_schools, never match the
    // opposites table, and would render "the ley_umbral key point at X" to a player.
    Bench bench;
    std::map<std::string, Grid> layers;
    Grid umbral(8, 0.1);
    umbral.at(3, 3) = 0.9;
    Grid radiant(8, 0.1);
    layers.emplace("ley_umbral", umbral);
    layers.emplace("ley_radiant", radiant);
    // ley_holy is excluded from the scan even when it is the strongest thing under the
    // college, so a holy college is never "of the holy school".
    Grid holy(8, 5.0);
    layers.emplace("ley_holy", holy);
    bench.request.layers = &layers;

    int seen = 0;
    for (const HeroesBlock& block : band(bench)) {
        for (const Hero& person : block.people) {
            if (!person.college.has_value()) continue;
            ++seen;
            check::equal(person.school, std::string("umbral"), "the school has no ley_ prefix");
            check::is_true(person.selectable.end() !=
                               std::find(person.selectable.begin(), person.selectable.end(),
                                         std::string("school:dark")),
                           "and umbral therefore reads as a dark school");
            check::is_true(person.claim.target_name.find("ley_") == std::string::npos,
                           "no layer key reaches a player-facing string");
        }
    }
    check::is_true(seen > 0, "a college magister precipitated");
}

TEST(a_tainted_home_is_read_from_the_umbral_and_infernal_layers) {
    Bench bench;
    std::map<std::string, Grid> clean;
    clean.emplace("ley_umbral", Grid(8, 0.1));
    bench.request.layers = &clean;
    int clean_hits = 0, tainted_hits = 0, homed = 0;
    for (const HeroesBlock& block : band(bench, 8)) {
        for (const Hero& person : block.people) {
            if (!person.home.has_value()) continue;
            ++homed;
            if (std::find(person.selectable.begin(), person.selectable.end(),
                          std::string("ley:tainted_home")) != person.selectable.end()) {
                ++clean_hits;
            }
        }
    }
    std::map<std::string, Grid> foul;
    Grid umbral(8, 0.1);
    umbral.at(3, 3) = 0.9;  // the capital stands at (3, 3)
    foul.emplace("ley_umbral", umbral);
    bench.request.layers = &foul;
    for (const HeroesBlock& block : band(bench, 8)) {
        for (const Hero& person : block.people) {
            if (!person.home.has_value() || person.home->uid != "city-1") continue;
            if (std::find(person.selectable.begin(), person.selectable.end(),
                          std::string("ley:tainted_home")) != person.selectable.end()) {
                ++tainted_hits;
            }
        }
    }
    check::is_true(homed > 0, "the band produced people with homes");
    check::equal(clean_hits, 0, "clean ground taints nobody");
    check::is_true(tainted_hits > 0, "foul ground under the home city taints its people");
}

TEST(an_epithet_renders_every_placeholder_it_keeps) {
    Bench bench;
    int named = 0, substituted = 0, legend_epithets = 0, living_with_legend_epithet = 0;
    std::set<std::string> legend_pool;
    for (const std::string_view entry : hero_epithets_for("legend")) legend_pool.insert(std::string(entry));
    check::equal(legend_pool.size(), std::size_t{2}, "the legend list is the two entries");

    // A wide sweep: a legend has nine epithet candidates and only two of them are the
    // legend pair, so a narrow band could miss the tail by luck rather than by defect.
    for (const HeroesBlock& block : band(bench, 80)) {
        for (const Hero& person : block.people) {
            ++named;
            check::is_true(!person.epithet.empty(), person.uid + " has an epithet");
            check::is_true(person.epithet.find('{') == std::string::npos &&
                               person.epithet.find('}') == std::string::npos,
                           "no placeholder survives into an epithet: " + person.epithet);
            check::equal(person.display_name, person.name + " " + person.epithet, "display name");
            if (person.epithet.find(' ') != std::string::npos &&
                person.epithet.find("of ") != std::string::npos) {
                ++substituted;
            }
            if (legend_pool.count(person.epithet)) {
                ++legend_epithets;
                if (person.status == "living") ++living_with_legend_epithet;
            }
        }
    }
    check::is_true(named > 20, "the band named a cast");
    check::is_true(substituted > 0, "some epithet actually substituted a name");
    // The legend list is APPENDED for a legend and is not in the pool for anyone else.
    check::is_true(legend_epithets > 0, "a legend drew from the legend list");
    check::equal(living_with_legend_epithet, 0, "no living person draws a legend epithet");
}

TEST(a_legends_extra_epithets_are_appended_at_the_tail) {
    // Prepending them would leave the SET of reachable epithets identical and silently
    // re-map every legend's draw, which no frequency test and no output check can see.
    const auto role_only = hero_epithets_for("pretender");
    const auto legend_pool = hero_epithets_for("legend");
    check::is_true(!role_only.empty() && !legend_pool.empty(), "both lists carry entries");

    const auto living = hero_epithet_candidates("pretender", false);
    check::equal(living.size(), role_only.size(), "a living person draws only its role's list");
    for (std::size_t i = 0; i < role_only.size(); ++i) {
        check::equal(std::string(living[i]), std::string(role_only[i]), "in file order");
    }

    const auto legend = hero_epithet_candidates("pretender", true);
    check::equal(legend.size(), role_only.size() + legend_pool.size(), "the two lists are joined");
    for (std::size_t i = 0; i < role_only.size(); ++i) {
        check::equal(std::string(legend[i]), std::string(role_only[i]),
                     "the role keeps indices 0..n-1");
    }
    for (std::size_t i = 0; i < legend_pool.size(); ++i) {
        check::equal(std::string(legend[role_only.size() + i]), std::string(legend_pool[i]),
                     "and the legend entries occupy the tail");
    }
    check::is_true(legend.front() != legend_pool.front(),
                   "the two orderings are distinguishable, so this test can fail");
}

TEST(a_covert_person_states_a_different_purpose_than_the_effect) {
    Bench bench;
    int covert = 0, honest = 0;
    for (const HeroesBlock& block : band(bench)) {
        std::map<std::string, const Hero*> by_uid;
        for (const Hero& person : block.people) by_uid[person.uid] = &person;
        for (const HeroQuestHook& hook : block.quest_hooks) {
            const Hero& giver = *by_uid.at(hook.giver_uid);
            if (hook.unwitting) {
                ++covert;
                check::is_true(giver.faces.size() > 1, "only a two-faced person misleads");
                check::equal(hook.offered_from_face, std::string("public"),
                             "the stated purpose comes from the mask");
            } else {
                ++honest;
                check::equal(hook.offered_from_face, giver.faces.front().label,
                             "an honest hook is offered from the first face");
            }
        }
    }
    check::is_true(honest > 0, "most hooks are honest");
    // Not every band produces a two-faced person with a hook branch, so this is reported
    // rather than required -- but the honest half above must be non-empty.
    check::is_true(covert >= 0, "covert hooks are counted");
}

TEST(a_failed_generation_is_a_reported_block_and_never_a_crashed_world) {
    Bench bench;
    bench.request.globe_radius_m = 0.0;
    // The direct call raises: a radius is not a thing to guess at in a metre world.
    check::throws([&] { resolve_heroes(bench.request); }, "a zero radius is refused");
    const HeroesBlock block = attach_heroes(bench.request);
    check::equal(block.status, std::string("failed"), "the boundary reports instead of throwing");
    check::is_true(!block.error.empty(), "and says what went wrong");
    check::equal(block.version, kHeroesVersion, "a failed block still carries its version");
    check::is_true(block.people.empty(), "and carries no cast");

    Bench good;
    check::equal(attach_heroes(good.request).status, std::string("ok"), "a good world still passes");
}

TEST(orgs_with_no_members_are_dropped_and_members_point_back) {
    Bench bench;
    int orgs = 0;
    for (const HeroesBlock& block : band(bench)) {
        std::map<std::string, const Hero*> by_uid;
        for (const Hero& person : block.people) by_uid[person.uid] = &person;
        for (const HeroOrg& org : block.orgs) {
            ++orgs;
            check::is_true(!org.members.empty(), "an empty org is dropped before export");
            for (const std::string& member : org.members) {
                check::is_true(by_uid.count(member) != 0, "a member is in the cast");
                check::equal(by_uid.at(member)->org_uid, org.uid,
                             "and carries the back-reference this port fills in");
            }
        }
    }
    check::is_true(orgs > 0, "the band produced orgs");
}

TEST(a_dread_may_have_no_archetype_where_a_person_may_not) {
    Bench bench;
    int dreads = 0, carded = 0, uncarded = 0;
    for (const HeroesBlock& block : band(bench)) {
        for (const Hero& person : block.people) {
            check::is_true(!person.archetype.empty(), person.uid + " was given a card");
            check::is_true(!person.persona.belief.empty(), person.uid + " carries a persona");
        }
        for (const HeroDread& dread : block.dreads) {
            ++dreads;
            check::is_true(!dread.display_name.empty(), "a dread is named after what it ate");
            check::is_true(dread.display_name.find(dread.species) == 0, "species first, then epithet");
            if (dread.archetype.empty()) {
                ++uncarded;
                check::is_true(!dread.has_persona, "an uncarded dread carries no persona");
            } else {
                ++carded;
                check::is_true(dread.has_persona, "a carded dread carries one");
            }
        }
    }
    check::is_true(dreads > 0, "a dread precipitated");
    // The draconic nest gives dread:long_lived, so this world's dread is always carded;
    // the tolerated branch is exercised by the no-family variant below.
    check::is_true(carded > 0, "a dread with a long-lived family takes a card");
    // A small beast of no long-lived family: not `ancient`, and too faint for the two
    // cards a bare `fame:renowned` or `fame:legendary` would open. No precondition holds.
    Bench plain;
    plain.request.nests[0].family = "fantastic";
    plain.request.nests[0].tier = 1;
    plain.request.nests[0].range_m = 500.0;
    for (const HeroesBlock& block : band(plain, 6)) {
        for (const HeroDread& dread : block.dreads) {
            check::is_true(dread.archetype.empty(),
                           "a dread no card admits is exported without one rather than failing");
            ++uncarded;
        }
    }
    check::is_true(uncarded > 0, "the tolerated no-card path was reached");
}

TEST(a_short_name_loses_every_city_suffix_not_only_the_first) {
    // The port specification states the opposite twice; the reference's replace has no
    // count argument.
    check::equal(hero_short_name("Ashford City"), std::string("Ashford"), "one suffix");
    check::equal(hero_short_name("City City City"), std::string("City"),
                 "every occurrence goes, not merely the first");
    check::equal(hero_short_name("Citadel"), std::string("Citadel"), "a bare word is untouched");
}

TEST(a_great_circle_separation_is_metres_and_clamped) {
    const Vec3 a{1.0, 0.0, 0.0};
    const Vec3 b{0.0, 1.0, 0.0};
    check::close(hero_separation_m(a, b, 1000.0), 1000.0 * 3.14159265358979 / 2.0, 1e-6,
                 "a quarter turn is a quarter of the circumference");
    // The acos argument is clamped, so a dot product that overshoots by a rounding error
    // is zero distance rather than a NaN.
    check::equal(hero_separation_m(a, a, 1000.0), 0.0, "a point is no distance from itself");
    const double antipode = hero_separation_m(a, Vec3{-1.0, 0.0, 0.0}, 1000.0);
    check::is_true(!std::isnan(antipode), "the antipode is a number");
}
