// Story web invariants.
//
// The web is compiled from the `heroes` block, so the bench here builds a small world,
// runs the hero generator over it and feeds the result in -- which is also the only way to
// test the one thing that matters most about this package: that it reads the exported
// block and nothing else.
//
// Nothing in the package is random except a per-hero initiative stagger, so most of these
// are exact rather than statistical.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fwg/story_web.hpp"

using namespace fwg;

namespace {

constexpr double kRadiusM = 11150.0;

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

// A world with enough shape to seat a sovereign, a council, a champion, a reeve, a keeper
// and a Dread -- and a ruin old enough to leave a legend.
struct Bench {
    HeroesRequest heroes;

    Bench() {
        heroes.seed = 42;
        heroes.final_age = 4;
        heroes.globe_radius_m = kRadiusM;
        heroes.spacing_m = 1000.0;

        HeroCity capital;
        capital.uid = "city-1";
        capital.id = "c1";
        capital.name = "Ashford";
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

        heroes.cities = {capital, medium};

        HeroRuin old_one;
        old_one.uid = "r0";
        old_one.id = "ruin-0";
        old_one.name = "Old Harrow";
        old_one.city_class = "capital";
        old_one.cause = "war_civil";
        old_one.destroyed_age = 1;
        old_one.civilization_id = "human_heartland";
        old_one.population_profile = "human_heartland";
        old_one.parent_race_id = "human";
        old_one.node = 23;
        old_one.x = 8;
        old_one.z = 2;
        old_one.has_direction = true;
        old_one.direction = east(0.13);

        HeroRuin fallen;
        fallen.uid = "r1";
        fallen.id = "ruin-1";
        fallen.name = "Dunmar";
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
        eaten.city_class = "medium";
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

        heroes.ruins = {old_one, fallen, eaten};

        HeroWar war;
        war.id = "war-1";
        war.age = 3;
        war.kind = "regional";
        war.victor_uid = "city-1";
        war.civilization_id = "human_heartland";
        war.defeated_uid = "r1";
        heroes.wars = {war};

        heroes.civilizations = {{"human_heartland", "the Heartlanders", "human"}};
        heroes.cultures = {{"cul-1", "human_heartland", {"c1", "c2"}}};

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
        heroes.nests = {wyrm};

        heroes.hamlets = {{"hamlet-7", "hamlet", "farming", "c1", 30, 3, 4}};
        heroes.fortresses = {{"fortress-2", "c1", 31, 4, 3, 5.0}};
        heroes.cores = {{"c1", 1.0}};
        heroes.gods = {{"god_weave", "the Weave"}};
        heroes.religion_sites = {{"shrine-1", "shrine", "god_weave", "ruin-1", 20, false}};
        heroes.threats = {{"city-1", 1.0}};
    }

    HeroesBlock cast(std::uint64_t seed = 42) const {
        HeroesRequest request = heroes;
        request.seed = seed;
        return resolve_heroes(request);
    }

    StoryWebRequest web(const HeroesBlock& block, std::uint64_t seed = 7) const {
        StoryWebRequest request;
        request.seed = seed;
        request.heroes = &block;
        request.site_ids = {{"c1", "city-1"}, {"c2", "city-2"}};
        StoryCityThreat threat;
        threat.city_uid = "city-1";
        threat.war_pressure = 1.0;
        threat.wars_recent = 1.0;
        threat.enemy_living = true;
        threat.war_risk = 0.4;
        threat.war_hunger = 0.2;
        threat.regional_threat = 0.7;
        request.threats = {threat};
        request.cores = {{"c1", 1.0}};
        request.coverage = {{"c1", 0.3}};
        request.node_intensity = {{"ruin-1-key", 2.0}};
        request.lunar_surge = true;
        request.lunar_hollow_night = false;
        return request;
    }
};

// A cast built by hand rather than generated.
//
// The tie-break contracts -- which conjunction a `missing` report names, which verb a
// thread asks for -- only show themselves on a fact set that makes several conjunctions
// fail by the same amount, and no generated world reliably produces one. This gives the
// test exact control of the fact set while still going through the real entry point.
struct Synthetic {
    HeroesBlock block;

    Synthetic() {
        block.status = "ok";
        block.final_age = 4;
        Hero person;
        person.uid = "hero-council-city-1-magistrate";
        person.role = "council";
        person.status = "living";
        person.civilization_id = "human_heartland";
        person.born_age = 4;
        person.tier = "notable";
        person.display_name = "Torsunn the Pinned";
        person.seat = "magistrate";
        person.claim = {"hold", "city-1", "Ashford"};
        person.alignment = hero_alignment_record(0.0, 0.0);
        person.faces.resize(1);
        person.selectable = {"role:council", "seat:magistrate", "seat:none"};
        block.people = {person};
        // The bond is what gives the hero `bond:rival`, which is the fact that makes all
        // three of `revenge`'s conjunctions fail by exactly one token.
        block.bonds = {{person.uid, "hero-other", "rival", "war-1"}};
    }

    StoryWebBlock compile() const {
        StoryWebRequest request;
        request.seed = 5;
        request.heroes = &block;
        return resolve_story_web(request);
    }
};

int living_count(const HeroesBlock& block) {
    int count = 0;
    for (const Hero& person : block.people) {
        if (person.status == "living") ++count;
    }
    for (const HeroDread& dread : block.dreads) {
        if (dread.status == "living") ++count;
    }
    return count;
}

}  // namespace

TEST(the_trope_catalogue_links_in_intact) {
    lint_story_catalogue();
    check::equal(story_tropes().size(), std::size_t{51}, "trope count");
    std::size_t acts = 0, options = 0, dread_admitting = 0;
    std::set<std::string_view> ids;
    for (const StoryTrope& trope : story_tropes()) {
        ids.insert(trope.id);
        check::equal(trope.acts.size(), std::size_t{3}, std::string(trope.id) + " has three acts");
        acts += trope.acts.size();
        for (const StoryAct& act : trope.acts) {
            check::between(static_cast<double>(act.options.size()), 2.0, 4.0,
                           std::string(trope.id) + " option count");
            check::is_true(act.abandonment_days >= 1, "a deadline is at least a day");
            check::is_true(!act.completion_target.empty() && !act.completion_state.empty(),
                           "a completion names a target in a state");
            options += act.options.size();
            for (const StoryOption& option : act.options) {
                check::between(option.delta.law, -0.5, 0.5, "an option's law delta is bounded");
                check::between(option.delta.good, -0.5, 0.5, "an option's good delta is bounded");
                check::is_true(!option.effect.empty(), "an option carries an effect tag");
            }
        }
        check::is_true(!trope.cast.empty(), "a trope names a cast");
        bool dreads = false;
        for (const std::string_view kind : trope.cast) {
            check::is_true(kind == "person" || kind == "dread", "the cast is closed");
            dreads |= kind == "dread";
        }
        if (dreads) ++dread_admitting;
        check::is_true(!trope.examples.empty(), "a trope carries prose examples");
        for (const HeroClause& clause : trope.requires_any) {
            for (const std::string_view token : clause.tokens) {
                const std::string_view bare = token[0] == '!' ? token.substr(1) : token;
                check::is_true(story_known_fact(bare),
                               std::string(trope.id) + " requires a known fact: " + std::string(token));
            }
        }
    }
    check::equal(ids.size(), std::size_t{51}, "trope ids are unique");
    check::equal(acts, std::size_t{153}, "153 acts, as the audit counted");
    check::equal(options, std::size_t{317}, "317 options -- not the ~450 the spec estimated");
    check::equal(dread_admitting, std::size_t{3}, "only three tropes admit a Dread");
}

TEST(the_two_copies_of_the_feature_vocabulary_are_identical) {
    // The reference kept the hero generator's FEATURES and the story web's HERO_FEATURES
    // as two literal copies in two packages, held in sync by comment alone. Both are
    // carried here as data, and this is what stops them drifting.
    const auto mine = story_hero_features();
    const auto theirs = hero_feature_vocabulary();
    check::equal(mine.size(), theirs.size(), "the two vocabularies are the same size");
    std::set<std::string_view> a(mine.begin(), mine.end());
    std::set<std::string_view> b(theirs.begin(), theirs.end());
    check::is_true(a == b, "the two vocabularies hold the same tokens");
    check::equal(a.size(), std::size_t{82}, "82 tokens, as both documents record");
}

TEST(the_quoted_pole_threshold_is_the_live_one) {
    // `weights.threshold` is read by NO code: it is a second copy of the hero generator's
    // live pole boundary, kept for the lab and the docs. Nothing in the reference tied the
    // two together, so this does.
    const double quoted = story_weight_policy().threshold;
    check::close(quoted, 0.34, 1e-12, "the quoted threshold");
    check::equal(hero_alignment_record(quoted, 0.0).code.substr(0, 1), std::string("N"),
                 "exactly at the quoted threshold a hero is still Neutral");
    check::equal(hero_alignment_record(quoted + 0.002, 0.0).code.substr(0, 1), std::string("L"),
                 "and a hair above it is Lawful");
    check::equal(hero_alignment_record(0.0, quoted + 0.002).code.substr(1, 1), std::string("G"),
                 "the good axis reads the same boundary");
}

TEST(the_constraint_default_is_unconstrained_and_a_null_verb_list_is_not_an_empty_one) {
    const StoryConstraint& fallback = story_constraints_for("no_such_archetype");
    check::is_true(!fallback.claim_locked && !fallback.drift_locked, "the default locks nothing");
    check::is_true(!fallback.has_allowed_verbs, "and allows any verb");
    check::is_true(story_constraints_for("").archetype.empty(), "no archetype lands on the default");

    const StoryConstraint& oath = story_constraints_for("oathbound");
    check::is_true(oath.claim_locked, "the oathbound may not change their claim");
    // NULL MEANS ANY VERB. An empty list would mean NO verb, and normalising one into the
    // other silences every constrained hero.
    check::is_true(!oath.has_allowed_verbs, "a claim-locked archetype still allows any verb");

    const StoryConstraint& hunter = story_constraints_for("obsessed_hunter");
    check::is_true(hunter.has_allowed_verbs, "the obsessed hunter names its verbs");
    check::equal(hunter.allowed_verbs.size(), std::size_t{1}, "exactly one");
    check::equal(std::string(hunter.allowed_verbs[0]), std::string("kill"), "and it is kill");
}

TEST(every_living_hero_is_woven_or_reported_with_its_nearest_misses) {
    Bench bench;
    int total = 0;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        const HeroesBlock cast = bench.cast(seed);
        const StoryWebRequest request = bench.web(cast);
        const StoryWebBlock block = resolve_story_web(request);
        check::equal(block.status, std::string("ok"), "the web compiled");
        check::equal(block.summary.woven + block.summary.unwoven, living_count(cast),
                     "every living hero is either woven or reported");
        total += block.summary.woven + block.summary.unwoven;
        for (const StoryUnwoven& row : block.unwoven) {
            check::is_true(row.nearest.size() <= 3, "the unwoven report caps at three near misses");
            check::is_true(!row.facts.empty(), "and says what the hero did carry");
            for (const StoryConsidered& miss : row.nearest) {
                check::is_true(!miss.missing.empty(), "a near miss names what failed");
            }
        }
        for (std::size_t i = 1; i < block.webs.size(); ++i) {
            check::is_true(block.webs[i - 1].uid < block.webs[i].uid, "the cast is sorted by uid");
        }
    }
    check::is_true(total > 30, "the sweep produced a cast worth weaving");
}

TEST(the_offered_spoke_is_the_heaviest_and_ties_break_on_trope_id) {
    Bench bench;
    int webs = 0, tie_checks = 0;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        const HeroesBlock cast = bench.cast(seed);
        const StoryWebBlock block = resolve_story_web(bench.web(cast));
        for (const StoryWebEntry& entry : block.webs) {
            ++webs;
            check::is_true(!entry.spokes.empty(), "a woven hero has spokes");
            check::equal(entry.offered, entry.spokes.front().trope_id, "the offer is spokes[0]");
            check::equal(entry.rest.hook_trope_id, entry.offered, "and so is the resting hook");
            for (std::size_t i = 1; i < entry.spokes.size(); ++i) {
                const StorySpoke& a = entry.spokes[i - 1];
                const StorySpoke& b = entry.spokes[i];
                check::is_true(a.weight > b.weight || (a.weight == b.weight && a.trope_id < b.trope_id),
                               "spokes descend by weight, ties on trope id");
                if (a.weight == b.weight) ++tie_checks;
            }
            check::equal(entry.rest.armed.size(), entry.spokes.size() - 1,
                         "everything but the offer is armed");
            for (std::size_t i = 0; i < entry.rest.armed.size(); ++i) {
                check::equal(entry.rest.armed[i].first, entry.spokes[i + 1].trope_id,
                             "in the same order");
            }
        }
    }
    check::is_true(webs > 20, "the sweep wove a cast");
    check::is_true(tie_checks > 0, "weights actually tie, so the trope-id tie-break is live");
}

TEST(the_exported_breakdown_reproduces_the_weight) {
    // The product is computed FROM THE ROUNDED PARTS so a consumer can re-derive it; a
    // version that rounded only at the end would export arithmetic that does not multiply
    // out.
    Bench bench;
    int checked = 0, boosted = 0, fitted = 0;
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        const HeroesBlock cast = bench.cast(seed);
        const StoryWebBlock block = resolve_story_web(bench.web(cast));
        for (const StoryWebEntry& entry : block.webs) {
            for (const StorySpoke& spoke : entry.spokes) {
                const StoryBreakdown& b = spoke.breakdown;
                const double product =
                    std::round(b.boosts * b.alignment_fit * b.archetype_fit * b.claim_fit * 10000.0) /
                    10000.0;
                check::close(spoke.weight, product, 1e-12,
                             spoke.trope_id + " weight reproduces from its breakdown");
                check::is_true(b.alignment_fit >= story_weight_policy().fit_floor,
                               "an opposed spoke is damped, never removed");
                ++checked;
                if (!b.boosted_by.empty()) ++boosted;
                if (b.archetype_fit != 1.0 || b.claim_fit != 1.0) ++fitted;
            }
        }
    }
    check::is_true(checked > 50, "the sweep produced spokes to re-derive");
    check::is_true(boosted > 0, "some spoke actually applied a boost");
    check::is_true(fitted > 0, "some spoke actually applied an archetype or claim multiplier");
}

TEST(a_neutral_pull_is_neither_favoured_nor_damped) {
    // |pull| is floored at pull_floor and the dot product is zero, so the fit is exactly 1.
    const HeroAlignment lawful_good = hero_alignment_record(0.9, 0.9);
    check::close(story_alignment_fit({0.0, 0.0}, lawful_good), 1.0, 1e-12, "a zero pull");
    // Agreement lifts, opposition damps, and the damping stops at the floor.
    const double with = story_alignment_fit({0.8, 0.8}, lawful_good);
    const double against = story_alignment_fit({-0.8, -0.8}, lawful_good);
    check::is_true(with > 1.0, "a pull the hero already leans into is favoured");
    check::is_true(against < 1.0, "an opposed one is damped");
    check::is_true(against >= story_weight_policy().fit_floor, "but never below the floor");
    const HeroAlignment extreme = hero_alignment_record(1.0, 1.0);
    check::close(story_alignment_fit({-1.0, -1.0}, extreme), story_weight_policy().fit_floor, 1e-12,
                 "a fully opposed spoke lands exactly on the floor");
}

TEST(a_dread_only_trope_never_reaches_a_person_and_the_reverse) {
    Bench bench;
    int dread_webs = 0, person_webs = 0;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        const HeroesBlock cast = bench.cast(seed);
        const StoryWebBlock block = resolve_story_web(bench.web(cast));
        for (const StoryWebEntry& entry : block.webs) {
            const std::string kind = entry.role == "dread" ? "dread" : "person";
            if (kind == "dread") ++dread_webs; else ++person_webs;
            for (const StorySpoke& spoke : entry.spokes) {
                const StoryTrope* trope = story_trope(spoke.trope_id);
                check::is_true(trope != nullptr, "the spoke names a real trope");
                bool admits = false;
                for (const std::string_view member : trope->cast) admits |= member == kind;
                check::is_true(admits, spoke.trope_id + " admits a " + kind);
            }
            for (const StoryThread& thread : entry.threads) {
                const StoryTrope* trope = story_trope(thread.to);
                bool admits = false;
                for (const std::string_view member : trope->cast) admits |= member == kind;
                check::is_true(admits, "a thread only leads somewhere this hero could go");
            }
        }
    }
    check::is_true(person_webs > 0, "people were woven");
    check::is_true(dread_webs > 0, "and so was a Dread");
}

TEST(threads_are_ranked_open_first_weight_next_and_capped) {
    Bench bench;
    int threads = 0, rewritten = 0, cut = 0;
    std::set<std::string> kinds;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        const HeroesBlock cast = bench.cast(seed);
        const StoryWebBlock block = resolve_story_web(bench.web(cast));
        for (const StoryWebEntry& entry : block.webs) {
            check::is_true(entry.threads.size() <=
                               static_cast<std::size_t>(story_weight_policy().max_threads_per_act),
                           "threads are capped -- ONCE PER HERO, whatever the field is called");
            for (std::size_t i = 1; i < entry.threads.size(); ++i) {
                const StoryThread& a = entry.threads[i - 1];
                const StoryThread& b = entry.threads[i];
                const int ra = story_thread_rank(a.condition_kind);
                const int rb = story_thread_rank(b.condition_kind);
                check::is_true(ra < rb || (ra == rb && (a.weight > b.weight ||
                                                        (a.weight == b.weight && a.to < b.to))),
                               "threads sort on (rank, -weight, trope id)");
            }
            for (const StoryThread& thread : entry.threads) {
                ++threads;
                kinds.insert(thread.condition_kind);
                check::is_true(story_thread_rank(thread.condition_kind) >= 0,
                               "a thread condition is one of the four kinds");
                check::is_true(thread.to != entry.offered, "no thread leads back to the offer");
                if (thread.condition_kind == "alignment") {
                    ++rewritten;
                    check::is_true(thread.axis == "good" || thread.axis == "law",
                                   "an alignment thread names an axis");
                    check::is_true(thread.toward == "up" || thread.toward == "down",
                                   "and a direction");
                    if (entry.drift_locked) {
                        check::is_true(!thread.cut.empty(),
                                       "a drift-locked archetype cannot move its alignment");
                    }
                }
                if (thread.condition_kind == "claim") {
                    check::is_true(!thread.verb.empty(), "a claim thread names the verb it needs");
                    if (entry.claim_locked) {
                        check::is_true(!thread.cut.empty(), "a claim-locked archetype cannot swap it");
                    } else if (entry.has_allowed_verbs) {
                        const bool allowed =
                            std::find(entry.allowed_verbs.begin(), entry.allowed_verbs.end(),
                                      thread.verb) != entry.allowed_verbs.end();
                        check::equal(thread.cut.empty(), allowed,
                                     "a verb outside the archetype's list is cut");
                    }
                }
                if (thread.condition_kind == "feature") {
                    check::between(static_cast<double>(thread.facts.size()), 1.0, 2.0,
                                   "a feature thread proposes one or two facts");
                    for (const std::string& fact : thread.facts) {
                        bool gainable = false;
                        for (const std::string_view entryv : story_gainable_features()) {
                            gainable |= entryv == fact;
                        }
                        check::is_true(gainable, "and only facts a runtime event can add: " + fact);
                    }
                }
                if (!thread.cut.empty()) ++cut;
            }
        }
    }
    check::is_true(threads > 40, "the sweep produced threads");
    check::is_true(kinds.size() >= 2, "more than one kind of way in was found");
    check::is_true(rewritten > 0, "an open thread was rewritten to an alignment one");
    check::is_true(cut > 0, "an archetype constraint actually cut a thread");
}

TEST(an_act_prompt_binds_the_heros_own_names_and_leaves_an_unfilled_slot_readable) {
    Bench bench;
    int acts = 0, substituted = 0, fell_back = 0;
    std::set<std::string_view> fallbacks;
    for (const StoryFallback& row : story_bind_fallbacks()) fallbacks.insert(row.text);
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        const HeroesBlock cast = bench.cast(seed);
        const StoryWebBlock block = resolve_story_web(bench.web(cast));
        for (const StoryWebEntry& entry : block.webs) {
            check::equal(entry.acts.size(), std::size_t{3}, "three acts are bound");
            for (std::size_t i = 0; i < entry.acts.size(); ++i) {
                const StoryBoundAct& act = entry.acts[i];
                ++acts;
                check::equal(act.ring, static_cast<int>(i) + 1, "the ring is the act's position");
                check::is_true(act.prompt.find('{') == std::string::npos,
                               "no slot survives unbound: " + act.prompt);
                // A prompt that began with a slot would otherwise open mid-sentence.
                check::is_true(act.prompt.empty() || !(act.prompt[0] >= 'a' && act.prompt[0] <= 'z'),
                               "a bound prompt opens with a capital: " + act.prompt);
                check::equal(act.completion_kind, std::string("target"), "completion kind");
                check::equal(act.abandonment_kind, std::string("deadline"), "abandonment kind");
                check::equal(act.implicit.size(), std::size_t{2}, "two implicit abandonments");
                check::equal(act.implicit[0], std::string("protagonist_dead"), "the first");
                check::equal(act.implicit[1], std::string("target_gone"), "the second");
                for (const std::string_view text : fallbacks) {
                    if (act.prompt.find(text) != std::string::npos) ++fell_back;
                }
                for (const StoryBoundOption& option : act.options) {
                    check::is_true(option.text.find('{') == std::string::npos,
                                   "nor in an option: " + option.text);
                    // `{hero}` occurs 62 times in OPTION texts and NEVER in a prompt, so
                    // the hero's own name only reaches a player through an option.
                    if (!entry.display_name.empty() &&
                        option.text.find(entry.display_name) != std::string::npos) {
                        ++substituted;
                    }
                }
            }
            check::equal(entry.rest.hook_act_id, entry.acts.front().id, "the hook is the first act");
            check::equal(entry.rest.hook_prompt, entry.acts.front().prompt, "with its bound prompt");
        }
    }
    check::is_true(acts > 30, "the sweep bound acts");
    // Both halves of the binder must actually fire, or this test proves nothing about it.
    check::is_true(substituted > 0, "a real name was substituted into a prompt");
    check::is_true(fell_back > 0, "and an unfilled slot rendered as readable English");
}

TEST(a_world_fact_reaches_only_the_heroes_whose_home_it_describes) {
    Bench bench;
    const HeroesBlock cast = bench.cast(6);
    const StoryWebBlock block = resolve_story_web(bench.web(cast));
    // Every fact the bench's threat row, rural core, coverage row and almanac can raise.
    const std::vector<std::string> homed = {"world:war_pressure", "world:war_recent",
                                            "world:enemy_living", "world:war_risk",
                                            "world:war_hunger", "world:threatened",
                                            "world:food_deficit", "world:famine"};
    std::set<std::string> seen_at_home, seen_away;
    int at_home = 0, away = 0, surges = 0;
    for (const StoryWebEntry& entry : block.webs) {
        const std::set<std::string> facts(entry.facts.begin(), entry.facts.end());
        // A single almanac event anywhere counts for EVERY hero. Deliberately coarse.
        if (facts.count("world:surge")) ++surges;
        check::is_true(facts.count("world:hollow_night") == 0,
                       "a hollow night nobody had does not arrive anyway");
        const bool lives_in_the_threatened_city = facts.count("home:set") != 0;
        for (const std::string& fact : homed) {
            if (!facts.count(fact)) continue;
            if (lives_in_the_threatened_city) {
                seen_at_home.insert(fact);
                ++at_home;
            } else {
                seen_away.insert(fact);
                ++away;
            }
        }
    }
    check::is_true(!block.webs.empty(), "the bench wove somebody");
    check::equal(surges, static_cast<int>(block.webs.size()), "the surge reached every hero");
    check::is_true(at_home > 0, "the threatened city's people carry its facts");
    // Only city-1 carries a threat row, a deficit and a coverage row in this bench, so a
    // hero with no home can carry none of the eight.
    check::equal(away, 0, "and a homeless hero carries none of them");
    check::is_true(seen_at_home.size() >= 6,
                   "most of the home-gated facts actually fired, so this is not vacuous");
    check::equal(seen_away.size(), std::size_t{0}, "nothing leaked");
}

TEST(a_famine_is_read_from_the_seasonal_model_and_not_from_the_deficit_diagnostic) {
    // "A famine is a year the city cannot feed itself, read from the seasonal food model,
    // not the structural deficit diagnostic (which marks nearly every city that lacks
    // farms)." The bench keeps a standing deficit on city-1 and moves only the coverage,
    // so the two facts must part company.
    Bench bench;
    const HeroesBlock cast = bench.cast(6);
    auto facts_with = [&](double coverage) {
        StoryWebRequest request = bench.web(cast);
        request.coverage = {{"c1", coverage}};
        const StoryWebBlock block = resolve_story_web(request);
        std::set<std::string> seen;
        int homed = 0;
        for (const StoryWebEntry& entry : block.webs) {
            const std::set<std::string> facts(entry.facts.begin(), entry.facts.end());
            if (!facts.count("home:set")) continue;
            ++homed;
            for (const std::string& fact : {std::string("world:famine"),
                                            std::string("world:food_deficit")}) {
                if (facts.count(fact)) seen.insert(fact);
            }
        }
        check::is_true(homed > 0, "the bench seated somebody in the city under test");
        return seen;
    };

    const std::set<std::string> hungry = facts_with(0.3);
    check::is_true(hungry.count("world:famine") != 0, "a city below the coverage floor is in famine");
    check::is_true(hungry.count("world:food_deficit") != 0, "and carries its structural deficit");

    const std::set<std::string> fed = facts_with(0.95);
    check::is_true(fed.count("world:food_deficit") != 0,
                   "the structural deficit is unchanged -- the city still has no farms");
    check::equal(fed.count("world:famine"), std::size_t{0},
                 "but a city that feeds itself this year is NOT in famine");

    // A city with no coverage row at all is not in famine either: absence is not zero.
    StoryWebRequest silent = bench.web(cast);
    silent.coverage.clear();
    const StoryWebBlock block = resolve_story_web(silent);
    for (const StoryWebEntry& entry : block.webs) {
        check::is_true(std::find(entry.facts.begin(), entry.facts.end(),
                                 std::string("world:famine")) == entry.facts.end(),
                       "no seasonal reading means no famine, not a famine of zero");
    }
    check::is_true(!block.webs.empty(), "and the sweep had webs to check");
}

TEST(the_only_draw_staggers_the_deadline_and_decides_nothing_else) {
    Bench bench;
    const HeroesBlock cast = bench.cast(3);
    const StoryWebBlock a = resolve_story_web(bench.web(cast, 11));
    const StoryWebBlock b = resolve_story_web(bench.web(cast, 9999));
    const StoryWebBlock again = resolve_story_web(bench.web(cast, 11));

    check::is_true(!a.webs.empty(), "the bench wove somebody");
    check::equal(a.webs.size(), b.webs.size(), "the cast does not depend on the seed");
    int moved = 0;
    for (std::size_t i = 0; i < a.webs.size(); ++i) {
        const StoryWebEntry& x = a.webs[i];
        const StoryWebEntry& y = b.webs[i];
        const StoryWebEntry& z = again.webs[i];
        check::equal(x.uid, y.uid, "same hero in the same place");
        check::equal(x.offered, y.offered, "the offer does not depend on the seed");
        check::equal(x.spokes.size(), y.spokes.size(), "nor the spoke count");
        check::equal(x.threads.size(), y.threads.size(), "nor the thread count");
        for (std::size_t j = 0; j < x.acts.size(); ++j) {
            check::equal(x.acts[j].prompt, y.acts[j].prompt, "nor the bound prose");
        }
        check::equal(x.rest.initiative_day, z.rest.initiative_day, "the same seed replays exactly");
        check::between(static_cast<double>(x.rest.initiative_day),
                       static_cast<double>(story_weight_policy().initiative_days),
                       static_cast<double>(story_weight_policy().initiative_days + 29),
                       "the stagger is 0..29 days past the base");
        check::equal(x.rest.expires_day, x.rest.initiative_day, "the hook expires on that day");
        if (x.rest.initiative_day != y.rest.initiative_day) ++moved;
    }
    check::is_true(moved > 0, "a different seed moved at least one deadline");
}

TEST(unreachable_tropes_are_listed_rather_than_hidden) {
    Bench bench;
    const HeroesBlock cast = bench.cast(5);
    const StoryWebBlock block = resolve_story_web(bench.web(cast));
    check::equal(block.summary.tropes, 51, "the catalogue is whole");
    check::equal(block.summary.reachable_tropes + static_cast<int>(block.unreachable_tropes.size()),
                 block.summary.tropes, "every trope is either reachable or listed");
    // With the shipped catalogue every trope has at least one satisfiable entry, so the
    // list is empty -- but the MECHANISM must still be exercised, or this passes for the
    // wrong reason.
    check::equal(block.unreachable_tropes.size(), std::size_t{0},
                 "no shipped trope is unreachable today");
    check::equal(story_never_computed().size(), std::size_t{3}, "three features nothing computes");
    int touched = 0;
    for (const StoryTrope& trope : story_tropes()) {
        for (const HeroClause& clause : trope.requires_any) {
            for (const std::string_view token : clause.tokens) {
                for (const std::string_view dead : story_never_computed()) {
                    if (token == dead) ++touched;
                }
            }
        }
    }
    check::is_true(touched > 0,
                   "some conjunction does name a never-computed feature, so the filter runs");
    for (const std::string_view dead : story_never_computed()) {
        bool known_feature = false;
        for (const std::string_view feature : hero_feature_vocabulary()) {
            known_feature |= feature == dead;
        }
        check::is_true(known_feature,
                       "a never-computed token is still a declared feature: " + std::string(dead));
    }
}

TEST(a_near_miss_names_the_first_cheapest_conjunction_not_the_last) {
    // `revenge` asks for claim:retake+bond:rival, claim:avenge, or claim:kill+bond:rival.
    // A hero who HAS bond:rival and claims `hold` fails all three by exactly one token, so
    // the report is decided entirely by which conjunction the walk keeps -- and the strict
    // less-than means the FIRST. 28 of the 51 tropes can tie like this.
    Synthetic bench;
    const StoryWebBlock block = bench.compile();
    check::equal(block.webs.size(), std::size_t{1}, "the synthetic hero was woven");
    const StoryWebEntry& entry = block.webs.front();
    check::is_true(std::find(entry.facts.begin(), entry.facts.end(), std::string("bond:rival")) !=
                       entry.facts.end(),
                   "the bond edge became a fact");
    check::is_true(std::find(entry.facts.begin(), entry.facts.end(), std::string("claim:hold")) !=
                       entry.facts.end(),
                   "and the claim verb did too");

    const StoryConsidered* revenge = nullptr;
    for (const StoryConsidered& row : entry.considered) {
        if (row.trope_id == "revenge") revenge = &row;
    }
    check::is_true(revenge != nullptr, "revenge was considered and rejected");
    check::equal(revenge->missing.size(), std::size_t{1}, "it fails by one token");
    check::equal(revenge->missing[0], std::string("claim:retake"),
                 "and the token named is the FIRST conjunction's, not the third's");

    // The same walk decides which verb a thread into `revenge` would ask for.
    const StoryThread* thread = nullptr;
    for (const StoryThread& row : entry.threads) {
        if (row.to == "revenge") thread = &row;
    }
    if (thread != nullptr) {
        check::equal(thread->condition_kind, std::string("claim"), "the way in is a claim swap");
        check::equal(thread->verb, std::string("retake"), "for the first conjunction's verb");
    }

    // The cap can push `revenge` out of the exported threads, so the general form of the
    // same property is asserted over every claim thread there is: only the ORDER of the
    // conjunction walk is restated here, and the gap test itself is the published one.
    int pinned = 0;
    for (const StoryThread& row : entry.threads) {
        if (row.condition_kind != "claim") continue;
        const StoryTrope* trope = story_trope(row.to);
        check::is_true(trope != nullptr, "the thread names a real trope");
        std::set<std::string> facts(entry.facts.begin(), entry.facts.end());
        std::string expected;
        std::size_t best = 0;
        bool have = false;
        for (const HeroClause& clause : trope->requires_any) {
            std::vector<std::string> gaps;
            for (const std::string_view token : clause.tokens) {
                if (!story_holds(token, facts)) gaps.emplace_back(token);
            }
            if (gaps.size() != 1 || gaps[0].rfind("claim:", 0) != 0) continue;
            if (!have || gaps.size() < best) {
                expected = gaps[0].substr(std::string("claim:").size());
                best = gaps.size();
                have = true;
            }
        }
        if (have) {
            check::equal(row.verb, expected, row.to + " asks for the first cheapest verb");
            ++pinned;
        }
    }
    check::is_true(pinned > 0, "claim threads exist, so the walk order is actually pinned");
}

TEST(an_exactly_tied_opposition_names_the_good_axis) {
    // A pull of (-0.2 law, -0.4 good) against an alignment of (0.4 law, 0.2 good) gives
    // -0.08 on each axis: an exact tie, and the sum is negative, so the pull opposes.
    const HeroAlignment alignment = hero_alignment_record(0.4, 0.2);
    const auto tied = story_opposing_axis({-0.2, -0.4}, alignment);
    check::is_true(tied.has_value(), "the pull opposes the hero");
    check::equal(tied->first, std::string("good"), "an exact tie is broken alphabetically");
    check::equal(tied->second, std::string("down"), "and a negative pull asks the axis to fall");

    // Break the tie the other way and the answer must move, or the test above proves
    // nothing about the comparison.
    const auto law_worse = story_opposing_axis({-0.9, -0.1}, alignment);
    check::is_true(law_worse.has_value(), "still opposed");
    check::equal(law_worse->first, std::string("law"), "the law axis suffers more here");
    const auto good_worse = story_opposing_axis({-0.05, -0.9}, alignment);
    check::equal(good_worse->first, std::string("good"), "and the good axis here");
    check::equal(story_opposing_axis({0.5, 0.5}, alignment).has_value(), false,
                 "a pull the hero leans into does not oppose at all");
    // The SUM decides: a spoke that drags one axis and lifts the other by as much agrees.
    check::equal(story_opposing_axis({-0.5, 1.0}, hero_alignment_record(1.0, 0.5)).has_value(),
                 false, "opposition is judged on the sum, not on either axis alone");
    check::equal(story_opposing_axis({0.1, -0.9}, alignment)->second, std::string("down"),
                 "a negative pull on the losing axis reads as down");
    // A positive pull on an axis the hero sits BELOW zero on still opposes, and asks the
    // axis to rise.
    const auto rising = story_opposing_axis({0.9, -0.1}, hero_alignment_record(-0.5, 0.9));
    check::is_true(rising.has_value(), "a positive pull against a negative axis opposes");
    check::equal(rising->first, std::string("law"), "on the axis that suffers most");
    check::equal(rising->second, std::string("up"), "and a positive pull reads as up");
}

TEST(the_summary_counts_what_it_exported_and_sorts_the_histogram) {
    Bench bench;
    const HeroesBlock cast = bench.cast(2);
    const StoryWebBlock block = resolve_story_web(bench.web(cast));
    check::equal(block.summary.woven, static_cast<int>(block.webs.size()), "woven count");
    check::equal(block.summary.unwoven, static_cast<int>(block.unwoven.size()), "unwoven count");
    int offered_total = 0;
    for (std::size_t i = 0; i < block.summary.offered.size(); ++i) {
        offered_total += block.summary.offered[i].second;
        if (i) {
            check::is_true(block.summary.offered[i - 1].first < block.summary.offered[i].first,
                           "the histogram is key-sorted, not insertion-ordered");
        }
    }
    check::equal(offered_total, block.summary.woven, "every woven hero was offered something");
    check::is_true(!block.summary.offered.empty(), "something was offered");

    std::size_t spokes = 0;
    for (const StoryWebEntry& entry : block.webs) spokes += entry.spokes.size();
    check::close(block.summary.mean_spokes,
                 std::round(static_cast<double>(spokes) / static_cast<double>(block.webs.size()) *
                            1000.0) / 1000.0,
                 1e-12, "mean spokes");
    check::equal(block.tropes.size(), std::size_t{51}, "the trope ledger is exported whole");
    check::equal(block.tropes.front().id, std::string(story_tropes().front().id),
                 "in raw catalogue order");
}

TEST(an_empty_cast_weaves_nothing_and_reports_zero_rather_than_a_nan) {
    HeroesBlock empty;
    empty.status = "ok";
    StoryWebRequest request;
    request.heroes = &empty;
    const StoryWebBlock block = resolve_story_web(request);
    check::equal(block.status, std::string("ok"), "an empty cast is not an error");
    check::equal(block.summary.woven, 0, "nothing woven");
    check::equal(block.summary.mean_spokes, 0.0, "mean spokes is zero, not a NaN");
    check::is_true(!std::isnan(block.summary.mean_spokes), "and definitely not a NaN");
    check::equal(block.summary.tropes, 51, "the catalogue is still reported");
}

TEST(a_missing_or_failed_hero_block_is_a_reported_failure) {
    StoryWebRequest none;
    check::throws([&] { resolve_story_web(none); }, "no heroes block is refused");
    check::equal(attach_story_web(none).status, std::string("failed"), "and reported, not thrown");

    HeroesBlock broken;
    broken.status = "failed";
    broken.error = "something gave way";
    StoryWebRequest request;
    request.heroes = &broken;
    const StoryWebBlock block = attach_story_web(request);
    check::equal(block.status, std::string("failed"), "a failed cast fails the web");
    check::is_true(block.error.find("something gave way") != std::string::npos,
                   "and carries the reason through");
    check::equal(block.version, kStoryWebVersion, "a failed block still carries its version");
}

TEST(the_web_carries_the_revision_of_the_cast_it_was_compiled_against) {
    Bench bench;
    const HeroesBlock cast = bench.cast(4);
    const StoryWebBlock block = resolve_story_web(bench.web(cast));
    check::equal(block.heroes_revision.archetypes, cast.policy_revision.archetypes,
                 "the archetype revision is carried across");
    check::equal(block.heroes_revision.wells, cast.policy_revision.wells, "and the wells one");
    check::is_true(block.policy_revision.tropes >= 1, "the trope catalogue states its own revision");
    check::is_true(!std::string(block.policy_revision.sha256).empty(),
                   "and a digest over the data it was built from");
    check::is_true(std::string(block.policy_revision.sha256) !=
                       std::string(cast.policy_revision.sha256),
                   "which is not the hero catalogue's");
}

TEST(the_only_dread_only_trope_ships_with_no_act_prompts) {
    // A recorded content gap, pinned so that filling it is noticed. `the_lair` is the only
    // trope a Dread can be offered alone, and all three of its acts carry an EMPTY prompt
    // string -- including the first, which becomes the Dread's resting hook. Every other
    // trope in the catalogue has prose.
    const StoryTrope* lair = story_trope("the_lair");
    check::is_true(lair != nullptr, "the_lair is in the catalogue");
    for (const StoryAct& act : lair->acts) {
        check::is_true(act.prompt.empty(), "the_lair act " + std::string(act.id) + " has no prompt");
        check::is_true(!act.title.empty(), "though it does have a title");
    }
    int empty_elsewhere = 0, prose = 0;
    for (const StoryTrope& trope : story_tropes()) {
        if (trope.id == "the_lair") continue;
        for (const StoryAct& act : trope.acts) {
            if (act.prompt.empty()) ++empty_elsewhere; else ++prose;
        }
    }
    check::equal(empty_elsewhere, 0, "no other trope ships an empty prompt");
    check::equal(prose, 150, "and the other 150 acts all carry one");
}
