#include "knowledge.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace orchestrator::knowledge {

namespace {

const std::vector<std::string> kPlaceCategories = {"settlements", "hamlets", "fortresses", "key_locations"};

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
    if (with_distance) f.fields.emplace_back("range", range_of(row.distance_m));
    return f;
}

Fact small_site_fact(const fwg::World& world, const fwg::NearSmallSiteRow& row, bool with_distance) {
    Fact f{FactKind::Place, row.id, {}};
    f.fields = {{"kind", row.kind}};
    // A hamlet or a fortress has no name of its own: it is known by the city it belongs to.
    const std::string of = city_name_of(world, row.core_id);
    if (!of.empty()) f.fields.emplace_back("of", of);
    f.fields.emplace_back("people", row.population_profile);
    if (with_distance) f.fields.emplace_back("range", range_of(row.distance_m));
    return f;
}

Fact key_location_fact(const fwg::NearKeyLocationRow& row, bool with_distance) {
    Fact f{FactKind::Place, row.id, {}};
    f.fields = {{"kind", row.kind}, {"name", row.name}, {"family", row.family}, {"state", row.state},
                {"threat", std::to_string(row.threat)}};
    if (!row.occupant.empty() && row.occupant != "none") f.fields.emplace_back("occupant", row.occupant);
    if (with_distance) f.fields.emplace_back("range", range_of(row.distance_m));
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
            // Whose sayings they know: their people, as the world records it.
            if constexpr (std::is_same_v<T, fwg::Hero>) persona.people = person.civilization_id;
            if constexpr (std::is_same_v<T, fwg::NpcPerson>)
                if (person.appearance) persona.people = person.appearance->civilization_id;
            if constexpr (std::is_same_v<T, fwg::Hero>) {
                if (const fwg::HeroFace* face = stranger_face(person)) {
                    persona.name = face->name;
                    persona.role = face->apparent_role;
                    if (with_card) {
                        take_card(persona, face->persona);
                        // One face hides nothing, so the archetype is how it speaks. A hero with two
                        // faces is shown face by face (show_face), by the director's rules.
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

}  // namespace

const fwg::HeroFace* stranger_face(const fwg::Hero& hero) {
    for (const std::string_view label : {"public", "first", "calm"})
        for (const fwg::HeroFace& face : hero.faces)
            if (face.label == label) return &face;
    return nullptr;
}

std::string place_label(const Fact& place) {
    if (const std::string* name = place.field("name")) return *name;
    const std::string* kind = place.field("kind");
    const std::string* of = place.field("of");
    if (kind && of) return "the " + *kind + " of " + *of;
    return kind ? "a " + *kind : std::string();
}

const fwg::Hero* hero_of(const Speaker& speaker) { return std::get_if<fwg::Hero>(&speaker.document.person); }

std::vector<std::pair<std::string, std::string>> ties_of(const fwg::Hero& hero) {
    std::vector<std::pair<std::string, std::string>> out;
    // The sovereign a castellan is sworn to (contract version 19), first.
    if (!hero.liege_uid.empty()) out.emplace_back("liege", hero.liege_uid);
    for (const auto& [tie, uids] : {std::pair<std::string, const std::vector<std::string>*>{"rival", &hero.rivals},
                                    {"kin", &hero.kin},
                                    {"ally", &hero.allies}})
        for (const std::string& uid : *uids) out.emplace_back(tie, uid);
    return out;
}

void show_face(Speaker& speaker, const fwg::Hero& hero, const fwg::HeroFace& face) {
    Persona& persona = speaker.persona;
    persona.name = face.name;
    persona.role = face.apparent_role;
    take_card(persona, face.persona);
    // A public face over a true one is a mask: its lines are its trade's, and its archetype is
    // what it hides. Every other face is who the hero is at that moment.
    const bool mask = face.label == "public" && hero.faces.size() > 1;
    persona.voice_key = mask ? std::string() : voice_key_of(hero.archetype, face.alignment.code);
    persona.face = face.label == "true" || face.label == "unleashed" || face.label == "second" ? face.label : std::string();
}

std::pair<std::string, std::string> known_as(const fwg::PersonDocument& document) {
    Persona seen;
    strangers_view(document, seen, /*with_card=*/false);
    return {seen.name, seen.role};
}

std::vector<int> place_nodes_within(const fwg::World& world, int node, double radius_m) {
    std::vector<int> out;
    const std::optional<fwg::NearRead> read = bounded_near(world, node, std::min(radius_m, near_ceiling(world)), kPlaceCategories);
    if (!read) return out;
    for (const auto& row : read->near.settlements) out.push_back(row.node);
    for (const auto& row : read->near.hamlets) out.push_back(row.node);
    for (const auto& row : read->near.fortresses) out.push_back(row.node);
    for (const auto& row : read->near.key_locations) out.push_back(row.node);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string range_of(double metres) {
    if (metres < 50.0) return "here";
    if (metres < 1000.0) return "close";
    if (metres < 3000.0) return "near";
    if (metres < 7000.0) return "walk";
    if (metres < 15000.0) return "far";
    return "distant";
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
                    {"range", range_of(nest.distance_m)}};
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<Fact> nests_around(const fwg::World& world, int node, double radius_m, std::size_t most) {
    std::vector<Fact> out;
    const std::optional<fwg::NearRead> read = bounded_near(world, node, radius_m, {"beast_nests"});
    if (!read) return out;
    std::vector<fwg::NearNestRow> nests = read->near.beast_nests;
    std::stable_sort(nests.begin(), nests.end(), [](const fwg::NearNestRow& a, const fwg::NearNestRow& b) {
        if (a.tier != b.tier) return a.tier > b.tier;
        return a.distance_m < b.distance_m;
    });
    for (const auto& nest : nests) {
        if (out.size() == most) break;
        Fact f{FactKind::Danger, nest.id, {}};
        f.fields = {{"creature", nest.name}, {"danger", std::to_string(nest.tier)}, {"family", nest.family},
                    {"range", range_of(nest.distance_m)}, {"around", "yes"}};
        out.push_back(std::move(f));
    }
    return out;
}

PlaceFolk folk_at(const fwg::World& world, const std::string& place_id, const std::string& speaker) {
    PlaceFolk out;
    fwg::PlaceReadRequest request;
    request.place_id = place_id;
    std::optional<fwg::PlaceRead> read;
    try {
        read = fwg::read_place(world, request);
    } catch (const fwg::WorldTimeError&) {
        return out;  // not a place the world can read in full: nothing more to say of it
    }
    // The roster's own posts first, as the roster marks them; a post a hero holds is the hero.
    std::vector<const fwg::NpcPostRow*> posts;
    for (const fwg::NpcPostRow& row : read->place.resolved.npc_posts)
        if (row.status == "alive") posts.push_back(&row);
    std::stable_partition(posts.begin(), posts.end(), [](const fwg::NpcPostRow* row) { return row->important; });
    for (const fwg::NpcPostRow* row : posts) {
        if (row->hero_uid) {
            if (*row->hero_uid != speaker && out.heroes.size() < 2 &&
                std::find(out.heroes.begin(), out.heroes.end(), *row->hero_uid) == out.heroes.end())
                out.heroes.push_back(*row->hero_uid);
            continue;
        }
        if (out.trades.size() < 3 && std::find(out.trades.begin(), out.trades.end(), row->post) == out.trades.end())
            out.trades.push_back(row->post);
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

namespace {

// When something was, against the world's latest age: "this age", "the last age" or "ages ago".
std::string age_band(const fwg::World& world, int age) {
    int now = 0;
    for (const fwg::AgeRecord& record : world.history.ages) now = std::max(now, record.age);
    if (age >= now) return "this age";
    return age == now - 1 ? "the last age" : "ages ago";
}

// What took a city, in a word a person would use: every war is "war", a hero's own magic "magic".
std::string loss_word(const std::string& cause) {
    if (cause.rfind("war", 0) == 0) return "war";
    if (cause == "self_magic") return "magic";
    if (cause == "radiant") return "radiance";
    return cause;
}

}  // namespace

std::vector<Fact> self_facts(const fwg::World& world, const Speaker& speaker, bool whole_story) {
    Fact self{FactKind::Self, speaker.uid, {}};
    const auto add = [&](const char* key, const std::string& value) {
        if (!value.empty()) self.fields.emplace_back(key, value);
    };
    add("role", speaker.persona.role);
    if (speaker.node)
        if (const std::optional<Fact> here = place_at(world, *speaker.node)) add("at", place_label(*here));
    std::vector<Fact> deeds, ties;
    const auto deeds_of = [&](std::vector<fwg::HeroDeed> all) {
        // Newest first; the record's own order among deeds of one age.
        std::stable_sort(all.begin(), all.end(), [](const fwg::HeroDeed& a, const fwg::HeroDeed& b) { return a.age > b.age; });
        for (const fwg::HeroDeed& d : all) {
            if (deeds.size() == kDeedsNamed) break;
            Fact deed{FactKind::Deed, d.event_id, {{"deed", d.event_kind}, {"part", d.role}}};
            if (const std::optional<std::string> place = anchor_name(world, d.event_id)) {
                deed.fields.emplace_back("place", *place);
            } else if (!d.city_name.empty()) {
                deed.fields.emplace_back("place", d.city_name);
            }
            if (d.event_kind == "war" && !d.kind.empty()) deed.fields.emplace_back("war", d.kind);
            // Whom the war was against (contract version 22): "what war, against whom" was a judge's first
            // ask of a castellan whose record says he won one (the golden benchmark, 2026-09-29).
            if (d.event_kind == "war")
                if (const std::optional<std::string> against = anchor_name(world, d.opponent_uid)) deed.fields.emplace_back("against", *against);
            deed.fields.emplace_back("when", age_band(world, d.age));
            deeds.push_back(std::move(deed));
        }
    };
    if (const fwg::Hero* hero = hero_of(speaker); hero && whole_story) {
        add("known_as", hero->epithet);
        add("fame", hero->tier);
        if (hero->home) add("home", hero->home->name);
        if (hero->presence) add("stands", hero->presence->situation);
        if (!hero->claim.verb.empty() && !hero->claim.target_name.empty()) {
            add("after", hero->claim.verb);
            add("after_what", hero->claim.target_name);
        }
        if (hero->lost) {
            add("lost", hero->lost->name);
            add("lost_to", loss_word(hero->lost->cause));
            add("lost_when", age_band(world, hero->lost->destroyed_age));
        }
        add("since", age_band(world, hero->born_age));
        // A castellan's past (contract version 19): the generator's record of them had held only the
        // post, and "How long have you held it?" had nothing to answer from.
        if (hero->tenure_years > 0) add("held_years", std::to_string(hero->tenure_years));
        add("served_at", hero->served_at);
        add("served_as", hero->served_as);
        deeds_of(hero->deeds);
        // A deed at the city they lost is known by that city's name: its ruin is no place the world's
        // blocks name, and "stood for Brenlin" says what "stood for its ruin" would not.
        if (hero->lost)
            for (Fact& deed : deeds)
                if (deed.subject == hero->lost->ruin_id && deed.field("place") == nullptr)
                    deed.fields.insert(deed.fields.begin() + 2, {"place", hero->lost->name});
        for (const auto& [tie, uid] : ties_of(*hero)) {
            if (ties.size() == kTiesNamed) break;
            if (std::optional<KnownPerson> person = person_fact(world, uid)) {
                person->fact.fields.emplace_back("tie", tie);
                ties.push_back(std::move(person->fact));
            }
        }
    } else if (const auto* dread = std::get_if<fwg::HeroDread>(&speaker.document.person); dread && whole_story) {
        add("fame", dread->tier);
        deeds_of(dread->deeds);
    }
    std::vector<Fact> out{std::move(self)};
    for (Fact& f : deeds) out.push_back(std::move(f));
    for (Fact& f : ties) out.push_back(std::move(f));
    return out;
}

}  // namespace orchestrator::knowledge
