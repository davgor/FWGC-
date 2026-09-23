#include "fwg/npcs.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "fwg/heritage.hpp"
#include "fwg/random.hpp"

namespace fwg {
namespace {

// Half-away-from-zero, matching key_locations. Only `important_fraction` uses it.
double round_to(double value, int places) {
    double scale = 1.0;
    for (int i = 0; i < places; ++i) scale *= 10.0;
    return std::round(value * scale) / scale;
}

bool is_digits(std::string_view text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// `plot-10` must not sort before `plot-2`: order on the number after the last '-', then on
// the raw id. Any non-numeric tail sorts after every numeric one.
std::pair<long long, std::string> plot_order(const NpcPlot& plot) {
    const std::size_t dash = plot.id.rfind('-');
    const std::string tail = dash == std::string::npos ? plot.id : plot.id.substr(dash + 1);
    if (is_digits(tail)) return {std::stoll(tail), plot.id};
    return {static_cast<long long>(plot.id.size()) + 1000000000LL, plot.id};
}

// The whole reachable name space for one people, built ONCE. Enumerating it is cheap for
// one people and ruinous thirteen thousand times, and the result is seedless -- rank is a
// property of the culture rather than of a seed, the way John was common in every English
// county -- so it caches on the pair that determines it and nothing else.
class NameSource {
public:
    std::string name_for(const std::string& civilization_id, const std::string& parent_race_id,
                         Rng& draw) {
        // NO FALLBACK. The silent one is exactly the defect that was removed here.
        if (civilization_id.empty()) {
            throw std::runtime_error("a person needs a civilization_id to be named from");
        }
        const Entry& entry = entry_for(civilization_id, parent_race_id);
        if (const auto drawn = heritage::stock_name(entry.stock, draw)) return drawn->name;
        // No template this people's roots can fill, which is an authoring fault in the
        // lexicon rather than a naming one. The reference coins through
        // `person_name_with_gloss`, which draws once and does NOT run the forbidden-form
        // filter -- a measured 138 leaks in 20,000 draws, and for one people two authored
        // shapes compose into an obscenity. This is one of the two paths that ships a name
        // to a player, so it coins through the FILTERED `person_name` instead. The gloss
        // the reference returns is discarded by its only caller, so nothing is lost.
        return heritage::person_name(entry.resolved, heritage::lexicon(), draw);
    }

private:
    struct Entry {
        heritage::Resolved resolved;
        heritage::NameStock stock;
    };

    const Entry& entry_for(const std::string& civilization_id, const std::string& parent_race_id) {
        const auto key = std::make_pair(civilization_id, parent_race_id);
        const auto found = stocks_.find(key);
        if (found != stocks_.end()) return found->second;
        heritage::Resolved resolved = heritage::resolve(civilization_id, parent_race_id);
        heritage::NameStock stock = heritage::name_stock(resolved, heritage::lexicon());
        return stocks_.emplace(key, Entry{std::move(resolved), std::move(stock)}).first->second;
    }

    std::map<std::pair<std::string, std::string>, Entry> stocks_;
};

// A site row with its plots, before it becomes an emitted site.
struct Row {
    std::string uid;
    std::string kind;
    std::string name;
    std::string civilization_id;
    std::string parent_race_id;
    std::string city_class;
    std::string plan_id;
    std::optional<int> node;
    NpcNumber x, z;
    bool has_plan_status = false;
    std::string plan_status;
    const std::vector<NpcPlot>* plots = nullptr;
};

std::string parent_race_of(const std::vector<NpcCivilizationEntity>& entities,
                           const std::string& civilization_id) {
    // Never guesses `human`: a wrong parent is a wrong tongue, and the silent human
    // fallback is precisely the defect that was removed.
    for (const NpcCivilizationEntity& entity : entities) {
        if (entity.id == civilization_id) return entity.parent_race_id;
    }
    return {};
}

std::string hamlet_name(const NpcHamletPlan& plan, const NpcSettlementSite* core) {
    const bool harbour =
        plan.kind == "coastal" || plan.role.find("harbor") != std::string::npos;
    const std::string word = harbour ? "harbour" : "farmstead";
    const std::string home = core != nullptr ? npc_short_name(core->name) : std::string();
    if (!home.empty()) return "the " + word + " below " + home;
    return "the " + word + " at node " + (plan.node ? std::to_string(*plan.node) : std::string());
}

// Two farmsteads below the same city would otherwise share a name. The uids already
// differ, so nothing joins wrongly -- but a person cannot be sent to "the fortress above
// Dunlin" when there are two, and the node is what actually separates them.
void disambiguate(std::vector<Row>& rows) {
    std::map<std::string, int> counts;
    for (const Row& row : rows) ++counts[row.name];
    for (Row& row : rows) {
        if (counts[row.name] > 1 && row.node) {
            row.name = row.name + " (" + std::to_string(*row.node) + ")";
        }
    }
}

std::vector<Row> collect(const NpcRequest& request, NpcNotes& notes) {
    std::map<std::string, const NpcSettlementSite*> by_uid;
    for (const NpcSettlementSite& site : request.settlements) {
        by_uid.emplace(site.uid, &site);
    }
    const auto settlement_at = [&](const std::optional<int>& index) -> const NpcSettlementSite* {
        if (!index || *index < 0 || *index >= static_cast<int>(request.settlements.size())) {
            return nullptr;
        }
        return &request.settlements[static_cast<std::size_t>(*index)];
    };
    std::map<std::string, const NpcFortress*> fortress_by_id;
    for (const NpcFortress& fortress : request.fortresses) {
        if (fortress.node) fortress_by_id.emplace(fortress.id, &fortress);
    }

    std::vector<Row> rows;
    for (const NpcCityPlan& plan : request.city_plans) {
        if (plan.city_uid.empty()) {
            notes.unanchored_sites += 1;  // costs one site, never the block
            continue;
        }
        const auto found = by_uid.find(plan.city_uid);
        const NpcSettlementSite* site = found != by_uid.end() ? found->second : nullptr;
        Row row;
        row.uid = plan.city_uid;
        row.kind = "city";
        row.name = !plan.name.empty() ? plan.name : plan.city_uid;
        row.civilization_id = !plan.civilization_id.empty()
                                  ? plan.civilization_id
                                  : (site != nullptr ? site->civilization_id : std::string());
        row.parent_race_id = (site != nullptr && !site->parent_race_id.empty())
                                 ? site->parent_race_id
                                 : parent_race_of(request.entities, row.civilization_id);
        if (plan.city_class == "capital" || plan.city_class == "medium" || plan.city_class == "small") {
            row.city_class = plan.city_class;
        }
        row.plan_id = plan.city_uid;
        if (site != nullptr) row.node = site->node;
        // KEY-ABSENCE, not falsy: a plan carrying `x: 0` keeps the 0.
        row.x = plan.x.present ? plan.x : (site != nullptr ? site->x : NpcNumber::absent());
        row.z = plan.z.present ? plan.z : (site != nullptr ? site->z : NpcNumber::absent());
        row.has_plan_status = true;
        row.plan_status = plan.status;
        row.plots = &plan.plots;
        rows.push_back(std::move(row));
    }

    for (const NpcHamletPlan& plan : request.hamlet_plans) {
        if (!plan.node) {
            notes.unanchored_sites += 1;
            continue;
        }
        const auto found = by_uid.find(plan.core_city_uid);
        const NpcSettlementSite* core = found != by_uid.end() ? found->second : nullptr;
        if (core == nullptr) core = settlement_at(plan.core_id);
        Row row;
        row.uid = "hamlet-node-" + std::to_string(*plan.node);
        row.kind = "hamlet";
        row.name = hamlet_name(plan, core);
        row.civilization_id = plan.civilization_id;
        row.parent_race_id = (core != nullptr && !core->parent_race_id.empty())
                                 ? core->parent_race_id
                                 : parent_race_of(request.entities, row.civilization_id);
        row.plan_id = plan.hamlet_id;
        row.node = plan.node;
        row.x = plan.x;
        row.z = plan.z;
        row.has_plan_status = true;
        row.plan_status = plan.status;
        row.plots = &plan.plots;
        rows.push_back(std::move(row));
    }

    for (const NpcCastlePlan& plan : request.castle_plans) {
        const auto found = fortress_by_id.find(plan.fortress_id);
        if (found == fortress_by_id.end()) {
            notes.unanchored_sites += 1;
            continue;
        }
        const NpcFortress& fortress = *found->second;
        const NpcSettlementSite* core = settlement_at(fortress.core_id);
        Row row;
        row.uid = "fortress-node-" + std::to_string(*fortress.node);
        row.kind = "fortress";
        const std::string home = core != nullptr ? npc_short_name(core->name) : std::string();
        row.name = !home.empty() ? "the fortress above " + home : "the fortress";
        row.civilization_id = fortress.population_profile;
        row.parent_race_id = (core != nullptr && !core->parent_race_id.empty())
                                 ? core->parent_race_id
                                 : parent_race_of(request.entities, row.civilization_id);
        row.plan_id = plan.fortress_id;
        row.node = fortress.node;
        row.x = plan.x;
        row.z = plan.z;
        row.has_plan_status = true;
        row.plan_status = plan.status;
        row.plots = &plan.plots;
        rows.push_back(std::move(row));
    }

    // A PLAIN string sort over mixed uid shapes, and it runs BEFORE the dedupe, so which
    // of two duplicate-uid rows survives is decided by their relative order in the
    // concatenated list -- which is stable only because the sort is stable.
    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row& a, const Row& b) { return a.uid < b.uid; });
    std::vector<Row> unique;
    std::set<std::string> seen;
    for (Row& row : rows) {
        if (!seen.insert(row.uid).second) {
            notes.duplicate_site_keys += 1;
            continue;
        }
        if (row.parent_race_id.empty()) notes.unresolved_parent_race += 1;
        unique.push_back(std::move(row));
    }
    disambiguate(unique);
    return unique;
}

std::string post_uid(const std::string& site_uid, std::string_view building_id, int ordinal) {
    return "npc-" + site_uid + "-" + std::string(npc_short_building(building_id)) + "-" +
           std::to_string(ordinal);
}

// The bounded important subset for one site: the senior posts of its best-ranked giver
// buildings. Heroes are earmarked separately and are NEVER subject to this cap; a cap that
// could silently demote a hero would be the bug.
std::set<std::string> earmark(const std::vector<NpcPost>& posts, const Row& row, NpcNotes& notes) {
    const int cap = row.kind == "city" ? npc_cap(row.city_class) : npc_cap(row.kind);
    // Absent and zero are indistinguishable here, and that is the reference's own test.
    if (cap <= 0) return {};
    struct Candidate {
        int rank;
        std::string_view building_id;
        int ordinal;
    };
    std::vector<Candidate> candidates;
    for (const NpcPost& post : posts) {
        if (!post.senior) continue;
        const int rank = npc_giver_index(post.building_id);
        if (rank < 0) continue;  // absent means never a giver, not rank 0
        if (!npc_role_verbed(post.role)) continue;
        candidates.push_back({rank, post.building_id, post.ordinal});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.building_id < b.building_id;  // redundant: rank is unique
    });
    notes.important_capped_out += std::max(0, static_cast<int>(candidates.size()) - cap);
    std::set<std::string> out;
    const std::size_t keep = std::min<std::size_t>(candidates.size(), static_cast<std::size_t>(cap));
    for (std::size_t i = 0; i < keep; ++i) {
        out.insert(post_uid(row.uid, candidates[i].building_id, candidates[i].ordinal));
    }
    return out;
}

struct PlaceEntry {
    int node = 0;
    std::optional<double> x, z;
};

// Every exported record that names a place, indexed by its own id, with its node.
//
// The coordinates are resolved HERE, where the whole world is in hand. That is the point:
// the producer holds the generating world and the consumer holds only the artifact, so a
// join the producer can do for free is one the consumer cannot do at all.
std::map<std::string, PlaceEntry> places(const NpcRequest& request) {
    std::map<std::string, PlaceEntry> index;
    const std::vector<const std::vector<NpcPlaceRow>*> sources = {
        &request.place_settlements, &request.place_hamlets,   &request.place_fortresses,
        &request.place_ports,       &request.place_religion_sites, &request.place_hero_camps,
        &request.place_colleges,    &request.place_nests,     &request.place_ruins};
    for (const std::vector<NpcPlaceRow>* source : sources) {
        for (const NpcPlaceRow& row : *source) {
            if (!row.node || row.uid.empty()) continue;
            index.emplace(row.uid, PlaceEntry{*row.node, row.x, row.z});  // FIRST WRITER WINS
        }
    }
    return index;
}

const PlaceEntry* locate(const std::map<std::string, PlaceEntry>& index, const std::string& uid) {
    if (uid.empty()) return nullptr;
    const auto direct = index.find(uid);
    if (direct != index.end()) return &direct->second;
    // Ruin presences name the city the ruin used to be.
    const auto ruin = index.find("ruin-" + uid);
    return ruin != index.end() ? &ruin->second : nullptr;
}

// A cumulative walk over integer weights, on one integer draw: exact, and the file order of
// the weights is the draw order.
std::string_view pick(std::span<const NpcWeight> rows, Rng& draw) {
    std::uint64_t total = 0;
    for (const NpcWeight& row : rows) total += static_cast<std::uint64_t>(row.weight);
    std::uint64_t at = draw.below(total);
    for (const NpcWeight& row : rows) {
        const auto weight = static_cast<std::uint64_t>(row.weight);
        if (at < weight) return row.id;
        at -= weight;
    }
    return rows.back().id;
}

// A person's gender and age, from its own stream: keyed on its uid alone, so neither moves
// when another person joins or leaves the roster. Absent when its people has no life.
std::optional<NpcAppearance> appearance_of(std::uint64_t seed, const std::string& uid, const std::string& civilization_id,
                                           const std::map<std::string, const NpcLifeSpan*>& spans) {
    const auto found = spans.find(civilization_id);
    if (civilization_id.empty() || found == spans.end()) return std::nullopt;
    const NpcLifeSpan& span = *found->second;
    Rng draw = Rng::child(seed, "npc-appearance-" + uid);
    NpcAppearance out;
    out.gender = pick(npc_genders(), draw);
    out.age_band = pick(npc_age_band_weights(), draw);
    for (const NpcAgeBand& band : span.bands) {
        if (band.id == out.age_band) out.age_years = static_cast<int>(draw.randint(band.from, band.to));
    }
    out.civilization_id = span.id;
    out.parent_race_id = span.parent_race_id;
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------

std::span<const std::string_view> npc_services(std::string_view role) {
    const auto rows = npc_role_services();
    const auto found = std::lower_bound(
        rows.begin(), rows.end(), role,
        [](const NpcRoleServices& row, std::string_view key) { return row.role < key; });
    return (found != rows.end() && found->role == role) ? found->services : std::span<const std::string_view>{};
}

NpcLifeSpan npc_life_span(std::string id, std::string parent_race_id, const NpcLifeStages& life) {
    const auto whole = [](double years) { return static_cast<int>(std::floor(years + 0.5)); };
    const int adult = whole(life.adult);
    const int elder = whole(life.elder);
    const int oldest = whole(life.max);
    const int middle = adult + whole((elder - adult) * npc_young_to_middle());
    NpcLifeSpan out;
    out.id = std::move(id);
    out.parent_race_id = std::move(parent_race_id);
    out.life = life;
    out.bands = {{"young", adult, middle - 1}, {"middle", middle, elder - 1}, {"old", elder, oldest}};
    for (const NpcAgeBand& band : out.bands) {
        if (band.from > band.to) {
            throw std::runtime_error("the life stages of " + out.id + " leave no room for its " + std::string(band.id) +
                                     " band");
        }
    }
    return out;
}

const NpcBuilding* npc_building(std::string_view id) {
    const auto rows = npc_buildings();
    const auto found = std::lower_bound(
        rows.begin(), rows.end(), id,
        [](const NpcBuilding& row, std::string_view key) { return row.id < key; });
    return (found != rows.end() && found->id == id) ? &*found : nullptr;
}

int npc_giver_index(std::string_view building_id) {
    const auto rank = npc_giver_rank();
    for (std::size_t i = 0; i < rank.size(); ++i) {
        if (rank[i] == building_id) return static_cast<int>(i);
    }
    return -1;
}

bool npc_role_verbed(std::string_view role) {
    const auto rows = npc_role_verbs();
    const auto found = std::lower_bound(
        rows.begin(), rows.end(), role,
        [](const NpcRoleVerbs& row, std::string_view key) { return row.role < key; });
    return found != rows.end() && found->role == role && !found->verbs.empty();
}

int npc_cap(std::string_view key) {
    for (const NpcCap& row : npc_caps()) {
        if (row.key == key) return row.cap;
    }
    return -1;
}

std::string_view npc_short_building(std::string_view building_id) {
    const std::size_t dot = building_id.find('.');
    return dot == std::string_view::npos ? building_id : building_id.substr(dot + 1);
}

std::string npc_short_name(std::string_view name) {
    const std::size_t age = name.find(" (Age ");
    std::string out(age == std::string_view::npos ? name : name.substr(0, age));
    const std::size_t first = out.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos) return {};
    const std::size_t last = out.find_last_not_of(" \t\n\r\f\v");
    return out.substr(first, last - first + 1);
}

std::vector<NpcPost> npc_posts_in(const std::vector<NpcPlot>& plots, int& mismatches) {
    std::vector<const NpcPlot*> ordered;
    ordered.reserve(plots.size());
    for (const NpcPlot& plot : plots) ordered.push_back(&plot);
    std::sort(ordered.begin(), ordered.end(), [](const NpcPlot* a, const NpcPlot* b) {
        return plot_order(*a) < plot_order(*b);
    });

    // Scoped to the whole SITE rather than to the plot, which is why only the first plot of
    // a given building in a site can produce a senior post.
    std::map<std::string_view, int> ordinals;
    std::vector<NpcPost> posts;
    for (const NpcPlot* plot : ordered) {
        if (plot->building_id.empty() || plot->workers <= 0) continue;
        const NpcBuilding* building = npc_building(plot->building_id);
        // A building the policy does not know yields nobody, silently. That is the intended
        // case for houses and unstaffed plots.
        if (building == nullptr || building->roles.empty()) continue;

        std::vector<std::string_view> expanded;
        for (const NpcRole& role : building->roles) {
            for (int i = 0; i < role.target; ++i) expanded.push_back(role.role);
        }
        if (expanded.empty()) {
            throw std::runtime_error("building " + plot->building_id +
                                     " expands to no posts; its roster has no positive target");
        }
        if (static_cast<int>(expanded.size()) != plot->workers) {
            // The PLOT'S COUNT ALWAYS WINS: it is what the planner budgeted and housed.
            // Never pad with a generic 'worker' -- padding is how a data bug becomes a
            // person -- so a short roster continues under its last role instead.
            mismatches += 1;
            if (static_cast<int>(expanded.size()) > plot->workers) {
                expanded.resize(static_cast<std::size_t>(plot->workers));
            } else {
                expanded.resize(static_cast<std::size_t>(plot->workers), expanded.back());
            }
        }
        for (std::size_t index = 0; index < expanded.size(); ++index) {
            const int ordinal = ordinals[building->id];
            ordinals[building->id] = ordinal + 1;
            posts.push_back({building->id, expanded[index], ordinal, plot->id,
                             index == 0 && ordinal == 0});
        }
    }
    return posts;
}

NpcRoster generate_npc_roster(const NpcRequest& request) {
    NpcRoster roster;
    roster.policy_revision_posts = npc_posts_revision();
    roster.policy_revision_names = npc_names_revision();
    roster.method = npc_method();
    roster.limits = npc_limits();
    roster.registry_world_revision = request.registry_revision;
    roster.registry_world_sha256 = request.registry_sha256;
    try {
        if (!request.has_city_plans && !request.has_hamlet_plans && !request.has_castle_plans) {
            throw std::runtime_error(
                "the npc roster needs at least one planned block; run the planners first");
        }
        NpcNotes notes;
        const std::vector<Row> rows = collect(request, notes);
        NameSource names;

        // Each people's life and the bands it gives, then each parent race's own.
        for (const NpcCivilizationEntity& e : request.entities) {
            if (e.life) roster.life_spans.push_back(npc_life_span(e.id, e.parent_race_id, *e.life));
        }
        for (const NpcParentRace& race : request.parent_races) {
            roster.parent_race_life_spans.push_back(npc_life_span(race.id, "", race.life));
        }
        const auto by_id = [](const NpcLifeSpan& a, const NpcLifeSpan& b) { return a.id < b.id; };
        std::sort(roster.life_spans.begin(), roster.life_spans.end(), by_id);
        std::sort(roster.parent_race_life_spans.begin(), roster.parent_race_life_spans.end(), by_id);
        std::map<std::string, const NpcLifeSpan*> spans;
        for (const NpcLifeSpan& span : roster.life_spans) spans.emplace(span.id, &span);

        for (const Row& row : rows) {
            std::vector<NpcPost> posts = npc_posts_in(*row.plots, notes.roster_mismatches);
            const std::set<std::string> important = earmark(posts, row, notes);
            std::map<std::string, const NpcPlot*> plot_by_id;
            for (const NpcPlot& plot : *row.plots) plot_by_id.emplace(plot.id, &plot);
            for (const NpcPost& post : posts) {
                NpcPerson person;
                person.uid = post_uid(row.uid, post.building_id, post.ordinal);
                if (!row.parent_race_id.empty()) {
                    // One stream per named person, used for exactly one name, so the number
                    // of people never becomes a shared-stream hazard.
                    Rng draw = Rng::child(request.seed, "npc-name-" + person.uid);
                    person.name = names.name_for(row.civilization_id, row.parent_race_id, draw);
                }
                person.site_uid = row.uid;
                person.site_kind = row.kind;
                person.post = post.role;
                person.building_id = std::string(post.building_id);
                person.plot_id = post.plot_id;
                person.alive = true;
                person.important = important.count(person.uid) != 0;
                if (const auto plot = plot_by_id.find(post.plot_id); plot != plot_by_id.end() && !plot->second->name.empty()) {
                    person.workplace = plot->second->name;
                }
                // Ruled 2026-09-23: one storekeeper post, told apart by the store it keeps.
                if (post.role == "storekeeper") person.store = std::string(npc_short_building(post.building_id));
                person.appearance = appearance_of(request.seed, person.uid, row.civilization_id, spans);
                if (!person.appearance) notes.appearance_unresolved += 1;
                roster.people.push_back(std::move(person));
            }
            NpcSite site;
            site.uid = row.uid;
            site.kind = row.kind;
            site.name = row.name;
            if (!row.civilization_id.empty()) site.civilization_id = row.civilization_id;
            if (!row.parent_race_id.empty()) site.parent_race_id = row.parent_race_id;
            if (!row.city_class.empty()) site.city_class = row.city_class;
            site.plan_id = row.plan_id;
            site.node = row.node;
            site.x = row.x;
            site.z = row.z;
            if (row.has_plan_status) site.plan_status = row.plan_status;
            site.posts = static_cast<int>(posts.size());
            roster.sites.push_back(std::move(site));
        }

        // The cast, folded in. A hero is not a staffed post, and three things follow that
        // the schema has to allow rather than pretend away: a presence may be absent
        // entirely, a presence may stand somewhere this block has no row for, and `post` is
        // the person's role rather than a job in a building.
        if (request.heroes_ok) {
            std::map<std::string, std::string> site_kinds;
            for (const Row& row : rows) site_kinds.emplace(row.uid, row.kind);
            const std::map<std::string, PlaceEntry> index = places(request);
            for (const NpcCastMember& member : request.cast) {
                if (member.uid.empty()) continue;
                std::optional<std::string> site_uid;
                for (const std::string* candidate : {&member.home_uid, &member.presence_uid}) {
                    if (!candidate->empty() && site_kinds.count(*candidate) != 0) {
                        site_uid = *candidate;
                        break;
                    }
                }
                if (!site_uid) notes.cast_outside_a_planned_site += 1;

                NpcPerson person;
                person.uid = std::string(kNpcUidPrefix) + member.uid;
                person.name = !member.display_name.empty()
                                  ? member.display_name
                                  : (!member.name.empty() ? member.name : member.uid);
                person.site_uid = site_uid;
                if (site_uid) person.site_kind = site_kinds.at(*site_uid);
                person.post = !member.role.empty() ? member.role : "hero";
                person.plot_id = "";
                person.alive = member.status == "living";
                person.important = true;  // never subject to the per-site cap
                person.is_cast = true;
                person.hero_uid = member.uid;
                person.appearance = appearance_of(request.seed, person.uid, member.civilization_id, spans);
                if (!person.appearance) notes.appearance_unresolved += 1;
                if (!member.presence_site_kind.empty()) person.presence_kind = member.presence_site_kind;
                if (!member.presence_uid.empty()) person.presence_uid = member.presence_uid;
                // Where they actually stand, whenever that is not the site they are filed
                // under. A castellan filed at their home city because their fortress keys
                // differently is a real place and the wrong one.
                if (!member.presence_uid.empty() &&
                    (!site_uid || member.presence_uid != *site_uid)) {
                    if (const PlaceEntry* where = locate(index, member.presence_uid)) {
                        person.presence_node = where->node;
                        // Omitted, not nulled, when the source record has only a node --
                        // religion sites are the live case.
                        if (where->x) {
                            person.presence_x = where->x;
                            person.presence_z = where->z;
                        }
                        notes.cast_located_by_presence += 1;
                    } else {
                        notes.cast_presence_unresolved += 1;
                    }
                }
                roster.people.push_back(std::move(person));
            }
        }

        // '~' sorts after every ASCII letter and digit, so null-site and null-building
        // records land last within their group rather than first.
        const auto sort_key = [](const NpcPerson& person) {
            return std::make_tuple(person.site_uid.value_or(std::string(kNpcNullSortKey)),
                                   person.building_id.value_or(std::string(kNpcNullSortKey)),
                                   person.uid);
        };
        std::sort(roster.people.begin(), roster.people.end(),
                  [&sort_key](const NpcPerson& a, const NpcPerson& b) {
                      return sort_key(a) < sort_key(b);
                  });

        roster.summary.total = static_cast<int>(roster.people.size());
        for (const NpcPerson& person : roster.people) {
            if (person.alive) ++roster.summary.alive;
            if (person.important) ++roster.summary.important;
            if (person.is_cast) ++roster.summary.heroes_linked;
        }
        roster.summary.dead = roster.summary.total - roster.summary.alive;
        roster.summary.important_fraction =
            roster.people.empty()
                ? 0.0
                : round_to(static_cast<double>(roster.summary.important) /
                               static_cast<double>(roster.people.size()), 4);
        roster.summary.sites = static_cast<int>(roster.sites.size());
        roster.summary.notes = notes;
        roster.ok = true;
    } catch (const std::exception& exc) {
        roster.ok = false;
        roster.error = exc.what();
        roster.sites.clear();
        roster.people.clear();
        roster.summary = {};
    }
    return roster;
}

}  // namespace fwg
