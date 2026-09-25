#include "knowledge.hpp"

#include <algorithm>
#include <cstdio>
#include <type_traits>
#include <variant>

namespace orchestrator::knowledge {

namespace {

const std::vector<std::string> kPlaceCategories = {"settlements", "hamlets", "fortresses", "key_locations"};

// The face a stranger sees: the public face, else the first of two, else the calm one. Never
// the true, second or unleashed face: those are what the first face hides.
const fwg::HeroFace* stranger_face(const fwg::Hero& hero) {
    for (const std::string_view label : {"public", "first", "calm"})
        for (const fwg::HeroFace& face : hero.faces)
            if (face.label == label) return &face;
    return nullptr;
}

// The written lines a hero speaks from: "warden/LN". Empty when either part is.
std::string voice_key_of(const std::string& archetype, const std::string& alignment_code) {
    return archetype.empty() || alignment_code.empty() ? std::string() : archetype + "/" + alignment_code;
}

// The card's voice, and only its voice: `lies`, `belief`, `wants`, `fears` and `tells` say what
// the character keeps to itself, and a voice handed them can say them.
void take_card(Persona& persona, const fwg::HeroPersona& card) {
    persona.manner = card.manner;
    persona.voice = card.voice;
    persona.toward_player = card.toward_player;
    persona.line = card.line;
    persona.never = card.never;
}

double near_ceiling(const fwg::World& world) { return world.effective.globe_radius * fwg::kNearRadiusFraction; }

std::string city_name_of(const fwg::World& world, int id) {
    for (const auto& site : world.settlements.sites)
        if (site.id == id) return site.name;
    return {};
}

Fact settlement_fact(const fwg::NearSettlementRow& row, bool with_distance) {
    Fact f{FactKind::Place, row.uid, {}};
    f.fields = {{"kind", "city"}, {"name", row.name}, {"class", std::string(row.city_class)}, {"people", row.population_profile}};
    if (with_distance) f.fields.emplace_back("distance", distance_text(row.distance_m));
    return f;
}

Fact small_site_fact(const fwg::World& world, const fwg::NearSmallSiteRow& row, bool with_distance) {
    Fact f{FactKind::Place, row.id, {}};
    f.fields = {{"kind", row.kind}};
    // A hamlet or a fortress has no name of its own: it is known by the city it belongs to.
    const std::string of = city_name_of(world, row.core_id);
    if (!of.empty()) f.fields.emplace_back("of", of);
    f.fields.emplace_back("people", row.population_profile);
    if (with_distance) f.fields.emplace_back("distance", distance_text(row.distance_m));
    return f;
}

Fact key_location_fact(const fwg::NearKeyLocationRow& row, bool with_distance) {
    Fact f{FactKind::Place, row.id, {}};
    f.fields = {{"kind", row.kind}, {"name", row.name}, {"family", row.family}, {"state", row.state},
                {"threat", std::to_string(row.threat)}};
    if (!row.occupant.empty() && row.occupant != "none") f.fields.emplace_back("occupant", row.occupant);
    if (with_distance) f.fields.emplace_back("distance", distance_text(row.distance_m));
    return f;
}

// A near read of the named categories that narrows its radius, halving, while the answer is
// over the row ceiling. nullopt only if even a small radius stays over it.
std::optional<fwg::NearRead> bounded_near(const fwg::World& world, int node, double radius_m,
                                  const std::vector<std::string>& categories) {
    fwg::NearReadRequest request;
    request.node = node;
    request.categories = categories;
    for (int attempt = 0; attempt < 8; ++attempt) {
        request.radius_m = radius_m;
        try {
            return fwg::read_near(world, request);
        } catch (const fwg::WorldTimeError& refusal) {
            if (refusal.code() != fwg::kRefusalStateCapacity) throw;
            radius_m /= 2.0;
        }
    }
    return std::nullopt;
}

double reach_of(const fwg::World& world, const fwg::PersonDocument& document) {
    double reach = kCityFolkReachM;
    std::visit(
        [&](const auto& person) {
            using T = std::decay_t<decltype(person)>;
            if constexpr (std::is_same_v<T, fwg::NpcPerson>) {
                reach = person.site_kind && *person.site_kind == "city" ? kCityFolkReachM : kHamletFolkReachM;
            } else if constexpr (std::is_same_v<T, fwg::Hero>) {
                reach = person.tier == "legendary" ? kLegendaryReachM
                        : person.tier == "renowned" ? kRenownedReachM
                                                    : kNotableReachM;
            } else if constexpr (std::is_same_v<T, fwg::HeroDread>) {
                reach = kDreadReachM;
            } else {
                reach = std::max(person.reach_m, kMinimumVillainReachM);
            }
        },
        document.person);
    return std::min(reach, near_ceiling(world));
}

// Name and role as a stranger would have them, and the card when there is one to take.
void strangers_view(const fwg::PersonDocument& document, Persona& persona, bool with_card) {
    std::visit(
        [&](const auto& person) {
            using T = std::decay_t<decltype(person)>;
            persona.uid = person.uid;
            if constexpr (std::is_same_v<T, fwg::Hero>) {
                if (const fwg::HeroFace* face = stranger_face(person)) {
                    persona.name = face->name;
                    persona.role = face->apparent_role;
                    if (with_card) {
                        take_card(persona, face->persona);
                        // One face hides nothing, so the archetype is how it speaks. A second face
                        // hides it (false_hero, deceiver), and that hero gets no voice key.
                        if (person.faces.size() == 1) persona.voice_key = voice_key_of(person.archetype, face->alignment.code);
                    }
                } else if (person.faces.empty()) {
                    // No faces at all: nothing is hidden, so the record is the face.
                    persona.name = person.name;
                    persona.role = person.role;
                    if (with_card) {
                        take_card(persona, person.persona);
                        persona.voice_key = voice_key_of(person.archetype, person.alignment.code);
                    }
                } else {
                    // Faces, and none a stranger sees: name only.
                    persona.name = person.name;
                }
            } else if constexpr (std::is_same_v<T, fwg::HeroDread>) {
                persona.name = person.display_name;
                persona.role = person.role;
                if (with_card && person.has_persona) take_card(persona, person.persona);
            } else if constexpr (std::is_same_v<T, fwg::NpcPerson>) {
                persona.name = person.name.value_or("");
                persona.role = person.post;
            } else {
                persona.role = "villain";
            }
        },
        document.person);
}

// A place fact as one line a person could say where something stands.
std::string place_label(const Fact& place) {
    if (const std::string* name = place.field("name")) return *name;
    const std::string* kind = place.field("kind");
    const std::string* of = place.field("of");
    if (kind && of) return "the " + *kind + " of " + *of;
    return kind ? "a " + *kind : std::string();
}

}  // namespace

std::string distance_text(double metres) {
    if (metres < 50.0) return "here";
    char text[32];
    std::snprintf(text, sizeof text, "%.1f km", metres / 1000.0);
    return text;
}

std::optional<Speaker> speaker(const fwg::World& world, const std::string& uid) {
    fwg::PersonReadRequest request;
    request.uid = uid;
    std::optional<fwg::PersonRead> read;
    try {
        read = fwg::read_person(world, request);
    } catch (const fwg::WorldTimeError& refusal) {
        if (refusal.field() == uid) return std::nullopt;  // the reference's convention: an unknown name is the field
        throw;
    }
    Speaker s;
    s.uid = uid;
    s.document = read->person;
    s.present = read->person.liveness == fwg::Liveness::Present;
    s.node = read->person.anchor.node;
    s.reach_m = reach_of(world, read->person);
    strangers_view(read->person, s.persona, /*with_card=*/true);
    if (s.node)
        if (const std::optional<Fact> here = place_at(world, *s.node)) s.persona.place = place_label(*here);
    return s;
}

std::vector<KnownPlace> places_in_reach(const fwg::World& world, Speaker& s) {
    std::vector<KnownPlace> out;
    if (!s.node || s.reach_m <= 0.0) return out;
    const std::optional<fwg::NearRead> read = bounded_near(world, *s.node, s.reach_m, kPlaceCategories);
    if (!read) {
        s.reach_m = 0.0;
        return out;
    }
    s.reach_m = read->radius_m;
    for (const auto& row : read->near.settlements) out.push_back({settlement_fact(row, true), row.node, row.distance_m});
    for (const auto& row : read->near.hamlets) out.push_back({small_site_fact(world, row, true), row.node, row.distance_m});
    for (const auto& row : read->near.fortresses) out.push_back({small_site_fact(world, row, true), row.node, row.distance_m});
    for (const auto& row : read->near.key_locations) out.push_back({key_location_fact(row, true), row.node, row.distance_m});
    std::stable_sort(out.begin(), out.end(), [](const KnownPlace& a, const KnownPlace& b) { return a.distance_m < b.distance_m; });
    return out;
}

std::vector<Fact> dangers_near(const fwg::World& world, const Speaker& s) {
    std::vector<Fact> out;
    if (!s.node || s.reach_m <= 0.0) return out;
    const std::optional<fwg::NearRead> read = bounded_near(world, *s.node, std::min(kDangerReachM, s.reach_m), {"beast_nests"});
    if (!read) return out;
    std::vector<fwg::NearNestRow> nests = read->near.beast_nests;
    // The worst first, then the nearest: what someone would warn about.
    std::stable_sort(nests.begin(), nests.end(), [](const fwg::NearNestRow& a, const fwg::NearNestRow& b) {
        if (a.tier != b.tier) return a.tier > b.tier;
        return a.distance_m < b.distance_m;
    });
    for (const auto& nest : nests) {
        if (out.size() == kMaxDangers) break;
        Fact f{FactKind::Danger, nest.id, {}};
        f.fields = {{"creature", nest.name}, {"danger", std::to_string(nest.tier)}, {"family", nest.family},
                    {"distance", distance_text(nest.distance_m)}};
        out.push_back(std::move(f));
    }
    return out;
}

std::optional<Fact> place_at(const fwg::World& world, int node) {
    fwg::NearReadRequest request;
    request.node = node;
    request.radius_m = 0.0;
    request.categories = kPlaceCategories;
    const fwg::NearRead read = fwg::read_near(world, request);
    // A city before a hamlet or a fortress, and any of them before a key location: what a
    // place is called is the largest thing standing there.
    if (!read.near.settlements.empty()) return settlement_fact(read.near.settlements.front(), false);
    if (!read.near.fortresses.empty()) return small_site_fact(world, read.near.fortresses.front(), false);
    if (!read.near.hamlets.empty()) return small_site_fact(world, read.near.hamlets.front(), false);
    if (!read.near.key_locations.empty()) return key_location_fact(read.near.key_locations.front(), false);
    return std::nullopt;
}

std::optional<std::string> anchor_name(const fwg::World& world, const std::string& anchor) {
    if (anchor.empty()) return std::nullopt;
    for (const auto& site : world.settlements.sites)
        if (site.uid == anchor) return site.name;
    const auto ruin_name = [&](const std::string& uid) -> std::optional<std::string> {
        for (const auto& ruin : world.ruins)
            if (ruin.uid == uid) return "the ruins of " + ruin.name;
        return std::nullopt;
    };
    if (const std::optional<std::string> ruin = ruin_name(anchor)) return ruin;
    const auto small_site = [&](const std::vector<fwg::RuralSite>& list) -> std::optional<std::string> {
        for (const fwg::RuralSite& site : list) {
            if (site.id != anchor && site.uid != anchor) continue;
            const std::string of = city_name_of(world, site.core_id);
            return of.empty() ? "a " + site.kind : "the " + site.kind + " of " + of;
        }
        return std::nullopt;
    };
    if (const std::optional<std::string> hamlet = small_site(world.humans.hamlets)) return hamlet;
    if (const std::optional<std::string> fortress = small_site(world.humans.fortresses)) return fortress;
    for (const auto& site : world.key_locations.sites)
        if (site.id == anchor) return site.name;
    for (const auto& nest : world.beast_nests.sites)
        if (nest.id == anchor) return "the nest of the " + std::string(nest.name);
    for (const auto& relic : world.heroes.relics) {
        if (relic.uid != anchor) continue;
        const std::optional<std::string> resting = ruin_name(relic.resting_at);
        return resting ? relic.name + ", in " + *resting : relic.name;
    }
    return std::nullopt;
}

std::optional<KnownPerson> person_fact(const fwg::World& world, const std::string& uid) {
    fwg::PersonReadRequest request;
    request.uid = uid;
    std::optional<fwg::PersonRead> read;
    try {
        read = fwg::read_person(world, request);
    } catch (const fwg::WorldTimeError& refusal) {
        if (refusal.field() == uid) return std::nullopt;
        throw;
    }
    if (std::holds_alternative<fwg::Villain>(read->person.person)) return std::nullopt;
    Persona seen;
    strangers_view(read->person, seen, /*with_card=*/false);
    Fact f{FactKind::Person, uid, {}};
    f.fields = {{"name", seen.name}, {"role", seen.role}};
    if (read->person.anchor.node)
        if (const std::optional<Fact> where = place_at(world, *read->person.anchor.node))
            f.fields.emplace_back("where", place_label(*where));
    f.fields.emplace_back("status", read->person.liveness == fwg::Liveness::Present ? "about" : "gone");
    return KnownPerson{std::move(f), read->person.anchor.node};
}

}  // namespace orchestrator::knowledge
