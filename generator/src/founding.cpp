#include "fwg/founding.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

#include "fwg/random.hpp"

namespace fwg {
namespace {

// The safety limit on rounds. Founding stops on its own when the target is met or when
// nothing can expand; this only bounds the pathological case.
constexpr int kMaxTurns = 128;

// How fast the search reach grows per round, and the cap on the exponent. 1.6^32 is
// already far past any world's width, so the cap only stops the power overflowing.
constexpr double kReachGrowth = 1.6;
constexpr int kMaxGrowthTurns = 32;

// [document: founding events] The reference's founding event is the founded site's own
// record with the round's fields added, so a reader of the events sees where a city came
// from without a join. Copied once the record is complete.
void carry_site(FoundingEvent& e, const FoundedSite& record) {
    e.founding_turn = record.founding_turn;
    e.founding_capital = record.founding_capital;
    e.founding_year = record.founding_year;
    e.migration_source_node = record.migration_source_node;
    e.source_civilization_id = record.source_civilization_id;
    e.migration_distance_m = record.migration_distance_m;
    e.cultural_branch = record.cultural_branch;
    e.diaspora = record.diaspora;
    e.diaspora_reason = record.diaspora_reason;
    e.diaspora_bonus = record.diaspora_bonus;
}

}  // namespace

FoundingResult found_cities(std::uint32_t seed, const FoundingRequest& request,
                            const std::function<double(int, int)>& distance) {
    const FoundingRules& rules = request.rules;
    if (rules.years_per_round <= 0) {
        throw std::invalid_argument("years_per_round must be positive");
    }
    if (rules.diaspora_interval_years <= 0 ||
        rules.diaspora_interval_years % rules.years_per_round != 0) {
        throw std::invalid_argument(
            "diaspora_interval_years (" + std::to_string(rules.diaspora_interval_years) +
            ") must be a positive multiple of years_per_round (" +
            std::to_string(rules.years_per_round) +
            "); the clock only lands on multiples of the round, so any other interval "
            "silently never fires");
    }
    if (rules.diaspora_bonus_per_parent != 0 && rules.diaspora_bonus_per_parent != 1) {
        throw std::invalid_argument("diaspora_bonus_per_parent must be 0 or 1");
    }

    FoundingResult out;
    out.sites = request.survivors;

    const std::set<int> blocked(request.forbidden.begin(), request.forbidden.end());
    std::set<std::string> bonus_used(request.diaspora_bonus_used.begin(),
                                     request.diaspora_bonus_used.end());
    std::set<std::string> ever_used(request.used_civilizations.begin(),
                                    request.used_civilizations.end());
    for (const FoundedSite& s : out.sites) ever_used.insert(s.population_profile);

    const auto minimum_for = [&](const std::string& key) {
        const auto it = request.minimum_residents.find(key);
        return it != request.minimum_residents.end() ? it->second
                                                     : request.rules.default_minimum_residents;
    };
    const auto allowance_for = [&](const std::string& key) {
        const auto it = request.food_allowances.find(key);
        return it != request.food_allowances.end() ? it->second : 0LL;
    };

    // A city needs people as well as ground. Where food allowances are known they cap
    // the quota outright: a people that cannot feed a second city does not found one.
    std::map<std::string, long long> quotas = request.quotas;
    if (!request.food_allowances.empty()) {
        for (auto& [key, value] : quotas) {
            const long long minimum = minimum_for(key);
            const long long supported = minimum > 0 ? allowance_for(key) / minimum : 0;
            value = std::min(value, supported);
        }
    }

    const double origin_gap =
        request.world_reach * request.rules.origin_min_separation_degrees / 180.0;

    // A later age resumes where the last one stopped, never on top of it.
    long long latest = request.rules.start_year;
    if (!out.sites.empty()) {
        long long newest = 0;
        for (const FoundedSite& s : out.sites) newest = std::max(newest, s.founding_year);
        latest = std::max(latest, newest + request.rules.years_per_round);
    }
    const long long year_offset = latest;

    long long target = 0;
    for (const auto& [key, value] : quotas) target += value;

    // The opening reach. Two cities' spacing, or a thirty-second of the world, whichever
    // is larger -- a world whose spacing is tiny still starts with a usable search.
    const double base = std::max({request.spacing * 2.0, request.world_reach / 32.0, 1.0});

    std::set<std::string> blocked_parents;

    const auto score_at = [&](const std::string& key, int node) {
        const auto it = request.scores.find(key);
        return it != request.scores.end() ? it->second[static_cast<std::size_t>(node)] : 0.0;
    };

    // Far enough from every standing city, and not on forbidden ground.
    const auto is_free = [&](int node) {
        if (blocked.count(node) != 0) return false;
        for (const FoundedSite& s : out.sites) {
            if (node == s.node) return false;
            if (distance(node, s.node) < request.spacing) return false;
        }
        return true;
    };

    // Every (civilization, node) this parent could still take.
    const auto available = [&](const std::string& race) {
        std::map<std::string, long long> counts;
        for (const auto& [key, owner] : request.owners) counts[key] = 0;
        for (const FoundedSite& s : out.sites) {
            const auto it = counts.find(s.population_profile);
            if (it != counts.end()) ++it->second;
        }
        std::vector<std::pair<std::string, int>> pool;
        for (const auto& [key, owner] : request.owners) {
            if (owner != race) continue;
            const auto quota = quotas.find(key);
            if (quota == quotas.end() || counts[key] >= quota->second) continue;
            const auto candidates = request.candidates.find(key);
            if (candidates == request.candidates.end()) continue;
            for (const int node : candidates->second) {
                if (is_free(node)) pool.emplace_back(key, node);
            }
        }
        return pool;
    };

    // Which civilizations could be carried somewhere new this interval.
    const auto diaspora_candidates = [&]() {
        std::map<std::string, std::vector<int>> options;
        if (request.food_allowances.empty()) return options;
        if (request.city_limit &&
            static_cast<long long>(out.sites.size()) >= *request.city_limit) {
            return options;
        }
        for (const auto& [key, race] : request.owners) {
            // A people already on the map does not need carrying, and one that cannot
            // feed a single city cannot be carried anywhere.
            if (ever_used.count(key) != 0) continue;
            if (allowance_for(key) < minimum_for(key)) continue;

            const auto quota = quotas.find(key);
            const bool bonus = quota == quotas.end() || quota->second == 0;
            if (bonus) {
                // The bonus is what lets a people with NO quota reach the map at all,
                // so it is spent at most once per parent and never past the ceiling.
                if (request.rules.diaspora_bonus_per_parent == 0) continue;
                if (bonus_used.count(race) != 0) continue;
                if (request.city_limit && target >= *request.city_limit) continue;
            } else if (static_cast<long long>(out.sites.size()) >= target) {
                continue;  // the ordinary rounds have already met the target
            }

            const auto candidates = request.candidates.find(key);
            if (candidates == request.candidates.end()) continue;
            std::vector<int> pool;
            for (const int node : candidates->second) {
                if (is_free(node)) pool.push_back(node);
            }

            bool has_members = false;
            for (const FoundedSite& s : out.sites) {
                if (s.parent_race_id == race) has_members = true;
            }
            // A parent's first city anywhere still has to stand clear of the other
            // parents' origins, diaspora or not.
            if (!has_members) {
                std::vector<int> separated;
                for (const int node : pool) {
                    bool ok = true;
                    for (const FoundedSite& s : out.sites) {
                        if (!s.founding_capital) continue;
                        if (distance(node, s.node) + 1e-8 < origin_gap) ok = false;
                    }
                    if (ok) separated.push_back(node);
                }
                pool = std::move(separated);
            }
            if (!pool.empty()) options[key] = std::move(pool);
        }
        return options;
    };

    std::string stop = "round_limit_reached";
    int turn = 1;
    for (; turn <= kMaxTurns; ++turn) {
        const double radius =
            std::min(request.world_reach,
                     base * std::pow(kReachGrowth, std::min(turn - 1, kMaxGrowthTurns)));
        bool possible = false;

        // Rotate initiative so one parent does not always claim contested sites first.
        std::vector<ParentRace> races = request.parents;
        if (!races.empty()) {
            const std::size_t offset = static_cast<std::size_t>(turn - 1) % races.size();
            std::rotate(races.begin(), races.begin() + static_cast<std::ptrdiff_t>(offset),
                        races.end());
        }

        for (const ParentRace& parent : races) {
            if (static_cast<long long>(out.sites.size()) >= target) break;
            // The table is authored, so its ids are views into static storage; the
            // records and keys below all own their strings.
            const std::string race_id(parent.id);
            if (blocked_parents.count(race_id) != 0) continue;

            std::vector<const FoundedSite*> members;
            for (const FoundedSite& s : out.sites) {
                if (s.parent_race_id == race_id) members.push_back(&s);
            }

            auto pool = available(race_id);
            const double pct = parent.founding_participation_percent;
            // A parent that never participates still gets its opening round, or it
            // could not exist at all.
            if (pool.empty() || ((turn > 1 || !members.empty()) && pct == 0.0)) continue;

            if (members.empty()) {
                std::vector<std::pair<std::string, int>> separated;
                for (const auto& pair : pool) {
                    bool ok = true;
                    for (const FoundedSite& s : out.sites) {
                        if (!s.founding_capital) continue;
                        if (distance(pair.second, s.node) + 1e-8 < origin_gap) ok = false;
                    }
                    if (ok) separated.push_back(pair);
                }
                if (separated.empty()) {
                    // No corner of the world is far enough from the peoples already
                    // here. This parent never founds; say so rather than retrying it
                    // every round for another 127 turns.
                    blocked_parents.insert(race_id);
                    FoundingEvent e;
                    e.turn = turn;
                    e.parent_race_id = race_id;
                    e.status = "no_separated_origin";
                    e.radius_m = request.world_reach;
                    e.required_separation_degrees = request.rules.origin_min_separation_degrees;
                    out.report.events.push_back(std::move(e));
                    continue;
                }
                pool = std::move(separated);
            }

            possible = true;
            Rng draw_rng = Rng::child(seed, "founding-" + std::to_string(turn) + "-" + race_id);
            const double draw = draw_rng.random() * 100.0;
            if ((turn > 1 || !members.empty()) && draw >= pct) {
                FoundingEvent e;
                e.turn = turn;
                e.parent_race_id = race_id;
                e.status = "did_not_participate";
                e.draw_percent = draw;
                e.radius_m = radius;
                out.report.events.push_back(std::move(e));
                continue;
            }

            // A civilization that has never founded is preferred over a second city for
            // one that has, so the map carries the variety the registry describes.
            std::vector<std::pair<std::string, int>> unused;
            for (const auto& pair : pool) {
                bool seen = false;
                for (const FoundedSite& s : out.sites) {
                    if (s.population_profile == pair.first) seen = true;
                }
                if (!seen) unused.push_back(pair);
            }
            if (!unused.empty()) pool = std::move(unused);

            // An established parent settles outward from where it already is.
            if (!members.empty()) {
                std::vector<std::pair<std::string, int>> within;
                for (const auto& pair : pool) {
                    double nearest = std::numeric_limits<double>::infinity();
                    for (const FoundedSite* s : members) {
                        nearest = std::min(nearest, distance(pair.second, s->node));
                    }
                    if (nearest <= radius) within.push_back(pair);
                }
                pool = std::move(within);
            }
            if (pool.empty()) {
                FoundingEvent e;
                e.turn = turn;
                e.parent_race_id = race_id;
                e.status = "search_radius_too_small";
                e.radius_m = radius;
                out.report.events.push_back(std::move(e));
                continue;
            }

            // Best ground wins; ties break on civilization then node, so the choice
            // never depends on the order the pool happened to be built in.
            const auto best = std::min_element(
                pool.begin(), pool.end(), [&](const auto& a, const auto& b) {
                    const double sa = score_at(a.first, a.second);
                    const double sb = score_at(b.first, b.second);
                    if (sa != sb) return sa > sb;
                    if (a.first != b.first) return a.first < b.first;
                    return a.second < b.second;
                });

            FoundedSite record;
            record.node = best->second;
            record.population_profile = best->first;
            record.parent_race_id = race_id;
            record.founding_turn = turn;
            record.founding_capital = members.empty();
            record.founding_year = year_offset + (turn - 1) * request.rules.years_per_round;

            // Settlers come from the nearest city this parent already holds.
            const FoundedSite* source = nullptr;
            for (const FoundedSite* s : members) {
                if (source == nullptr) {
                    source = s;
                    continue;
                }
                const double d = distance(record.node, s->node);
                const double best_d = distance(record.node, source->node);
                if (d < best_d || (d == best_d && s->node < source->node)) source = s;
            }
            if (source != nullptr) {
                record.migration_source_node = source->node;
                record.source_civilization_id = source->population_profile;
                record.migration_distance_m = distance(record.node, source->node);
                record.cultural_branch = source->population_profile != record.population_profile;
            }

            FoundingEvent e;
            e.turn = turn;
            e.parent_race_id = race_id;
            e.status = "founded";
            e.radius_m = members.empty() ? request.world_reach : radius;
            e.suitability = score_at(record.population_profile, record.node);
            e.node = record.node;
            e.population_profile = record.population_profile;
            carry_site(e, record);
            out.report.events.push_back(std::move(e));

            ever_used.insert(record.population_profile);
            out.sites.push_back(std::move(record));
        }

        const long long year = year_offset + (turn - 1) * request.rules.years_per_round;
        auto options = diaspora_candidates();
        if (!options.empty() && year > 0 && request.rules.diaspora_interval_years > 0 &&
            year % request.rules.diaspora_interval_years == 0) {
            Rng rng = Rng::child(seed, "diaspora-" + std::to_string(year));
            std::vector<std::string> keys;
            for (const auto& [key, pool] : options) keys.push_back(key);
            const std::string key = keys[static_cast<std::size_t>(rng.below(keys.size()))];
            const std::string race = request.owners.at(key);

            int node = options[key].front();
            for (const int n : options[key]) {
                const double a = score_at(key, n);
                const double b = score_at(key, node);
                if (a > b || (a == b && n < node)) node = n;
            }

            std::vector<const FoundedSite*> members;
            for (const FoundedSite& s : out.sites) {
                if (s.parent_race_id == race) members.push_back(&s);
            }
            const FoundedSite* source = nullptr;
            for (const FoundedSite* s : members) {
                if (source == nullptr) {
                    source = s;
                    continue;
                }
                const double d = distance(node, s->node);
                const double best_d = distance(node, source->node);
                if (d < best_d || (d == best_d && s->node < source->node)) source = s;
            }

            const bool bonus = quotas[key] == 0;
            if (bonus) {
                quotas[key] = 1;
                ++target;
                bonus_used.insert(race);
            }

            std::vector<std::string> reasons{"exile", "religious_schism", "expedition"};
            if (request.rules.magic_enabled) reasons.push_back("magical_displacement");

            FoundedSite record;
            record.node = node;
            record.population_profile = key;
            record.parent_race_id = race;
            record.founding_turn = turn;
            record.founding_capital = members.empty();
            record.founding_year = year;
            if (source != nullptr) {
                record.migration_source_node = source->node;
                record.source_civilization_id = source->population_profile;
                record.migration_distance_m = distance(node, source->node);
            }
            // A diaspora is a branch whenever it came from somewhere, even from its own
            // civilization: the people who left are no longer the people who stayed.
            record.cultural_branch = source != nullptr;
            record.diaspora = true;
            record.diaspora_reason = reasons[static_cast<std::size_t>(rng.below(reasons.size()))];
            record.diaspora_bonus = bonus;

            FoundingEvent e;
            e.turn = turn;
            e.parent_race_id = race;
            e.status = "diaspora_founded";
            e.radius_m = request.world_reach;
            e.suitability = score_at(key, node);
            e.node = node;
            e.population_profile = key;
            carry_site(e, record);
            out.report.events.push_back(std::move(e));

            ever_used.insert(key);
            out.sites.push_back(std::move(record));
            options = diaspora_candidates();
        }

        // A pending diaspora is a reason to keep going even when the rounds are spent.
        if (!options.empty()) possible = true;
        if (static_cast<long long>(out.sites.size()) >= target && options.empty()) {
            stop = "capacity_target_reached";
            break;
        }
        if (!possible) {
            stop = "no_eligible_expansion";
            break;
        }
    }
    turn = std::min(turn, kMaxTurns);

    for (const auto& [key, race] : request.owners) {
        (void)race;
        out.report.minimum_residents[key] = minimum_for(key);
    }
    out.report.years_per_round = request.rules.years_per_round;
    out.report.diaspora_interval_years = request.rules.diaspora_interval_years;
    out.report.origin_min_separation_degrees = request.rules.origin_min_separation_degrees;
    out.report.effective_quotas = quotas;
    out.report.diaspora_bonus_used.assign(bonus_used.begin(), bonus_used.end());
    out.report.used_civilizations.assign(ever_used.begin(), ever_used.end());
    out.report.start_year = year_offset;
    out.report.elapsed_years = (turn - 1) * request.rules.years_per_round;
    out.report.end_year = year_offset + out.report.elapsed_years;
    out.report.turns = turn;
    out.report.target_cities = target;
    out.report.placed_cities = static_cast<long long>(out.sites.size());
    out.report.stop_reason = stop;
    return out;
}

}  // namespace fwg
