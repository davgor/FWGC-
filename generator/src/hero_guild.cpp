#include "fwg/hero_guild.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "fwg/city_plan.hpp"
#include "fwg/civilizations.hpp"
#include "fwg/patch.hpp"
#include "fwg/product_content.hpp"
#include "fwg/request_json.hpp"

namespace fwg {
namespace {

constexpr std::string_view kRequestFields[] = {"civilization_id",        "city_block",
                                               "population",             "visiting_heroes_requested",
                                               "available_jobs",         "already_housed_resident_heroes",
                                               "available_visitor_beds"};
constexpr std::string_view kPolicyFields[] = {"resident_rate_numerator", "resident_rate_denominator",
                                              "max_resident_heroes",     "max_visiting_heroes",
                                              "party_size",              "max_active_parties",
                                              "heroes_per_service_worker", "max_additional_staff"};
constexpr std::string_view kCityBlocks[] = {"small_city", "medium_city", "capital_city"};

template <std::size_t N>
void require_exact(const JsonValue& value, const std::string_view (&fields)[N], const char* what) {
    bool ok = value.is_object() && value.size() == N;
    for (const std::string_view f : fields) ok = ok && value.contains(f);
    if (!ok) {
        throw PatchError(refusal::cross_field(std::string("unexpected or missing ") + what + " fields",
                                              std::vector<std::string_view>(std::begin(fields), std::end(fields))));
    }
}

std::int64_t count(const JsonValue& value, const std::string& field) {
    if (!value.is_integer()) throw PatchError(refusal::wrong_type(field, value, "integer"));
    if (!value.is_int64() || value.as_int() < 0 || value.as_int() > kGuildMaxCount) {
        throw PatchError(refusal::out_of_range(field, value, 0.0, static_cast<double>(kGuildMaxCount), "integer", ""));
    }
    return value.as_int();
}

// The reference's `_policy`: exact fields, bounded counts, a usable rate and capacity.
JsonValue checked_policy(const JsonValue& policy, const std::string& prefix) {
    require_exact(policy, kPolicyFields, "guild policy");
    for (const std::string_view field : kPolicyFields) count(policy.at(field), prefix + std::string(field));
    const auto get = [&](std::string_view f) { return policy.at(f).as_int(); };
    if (get("resident_rate_denominator") == 0 || get("resident_rate_numerator") > get("resident_rate_denominator") ||
        get("party_size") < 1 || get("party_size") > 64 || get("heroes_per_service_worker") == 0) {
        throw PatchError(refusal::cross_field(
            "invalid guild rate or capacity: the denominator must be positive and no smaller than the numerator, the "
            "party 1..64 and heroes_per_service_worker positive",
            {"resident_rate_numerator", "resident_rate_denominator", "party_size", "heroes_per_service_worker"}));
    }
    return policy;
}

// The guild hall's staffing basis, from the structure libraries it was authored in.
const JsonValue* hall_staffing() {
    static const JsonValue sources = parse_json(asset_sources_json());
    for (const auto& [library, content] : sources.at("structure_blocks").as_object()) {
        for (const JsonValue& block : content.at("blocks").as_array()) {
            for (const JsonValue& s : block.at("structures").as_array()) {
                if (s.at("id").as_string() == "building.guildhall") return &s.at("staffing");
            }
        }
    }
    return nullptr;
}

[[noreturn]] void no_hall(std::string message) {
    PatchRefusal r;
    r.kind = "refused_by_world";
    r.field = "city_block";
    r.refused_by_world = true;
    r.message = std::move(message);
    throw PatchError(std::move(r));
}

}  // namespace

JsonValue guild_policy_document() {
    JsonValue document = parse_json(hero_guild_policies_json());
    static constexpr std::string_view kDocumentFields[] = {"schema", "schema_version", "revision", "policies"};
    require_exact(document, kDocumentFields, "guild policy document");
    const JsonValue& version = document.at("schema_version");
    if (document.at("schema") != JsonValue("fantasy-world-generator.hero-guild-policies") || !version.is_int64() ||
        version.as_int() != 1) {
        throw PatchError(refusal::unsupported_api("schema_version", version, {JsonValue(1)}));
    }
    if (count(document.at("revision"), "revision") == 0) {
        throw PatchError(refusal::out_of_range("revision", document.at("revision"), 1.0,
                                               static_cast<double>(kGuildMaxCount), "integer", ""));
    }
    const JsonValue& policies = document.at("policies");
    std::set<std::string> have;
    if (policies.is_object()) {
        for (const auto& member : policies.as_object()) have.insert(member.first);
    }
    std::set<std::string> want;
    for (const std::string_view id : civilization_ids()) want.emplace(id);
    if (!policies.is_object() || have != want) {
        throw PatchError(refusal::cross_field("guild policies must cover exactly the current civilizations", {"policies"}));
    }
    for (const auto& [id, policy] : policies.as_object()) checked_policy(policy, "policies." + id + ".");
    return document;
}

JsonValue plan_hero_guild(const JsonValue& request, const JsonValue* policy) {
    require_exact(request, kRequestFields, "guild planning");
    const JsonValue& civilization = request.at("civilization_id");
    const JsonValue& block = request.at("city_block");
    if (!civilization.is_string()) throw PatchError(refusal::wrong_type("civilization_id", civilization, "string"));
    if (!block.is_string()) throw PatchError(refusal::wrong_type("city_block", block, "string"));
    for (const std::string_view field : kRequestFields) {
        if (field != "civilization_id" && field != "city_block") count(request.at(field), std::string(field));
    }

    // The selected preset, resolved so its two workers are never added a second time.
    const auto block_at = std::find(std::begin(kCityBlocks), std::end(kCityBlocks), block.as_string());
    if (block_at == std::end(kCityBlocks)) {
        throw PatchError(refusal::invalid_choice("city_block", block, {"small_city", "medium_city", "capital_city"}));
    }
    std::vector<std::string> civilizations;
    for (const CivilizationPresets& presets : civilization_presets()) civilizations.emplace_back(presets.id);
    if (std::find(civilizations.begin(), civilizations.end(), civilization.as_string()) == civilizations.end()) {
        throw PatchError(refusal::invalid_choice("civilization_id", civilization, civilizations));
    }
    const SettlementPreset& preset = settlement_preset(
        civilization.as_string(), static_cast<SettlementBlock>(block_at - std::begin(kCityBlocks)));
    std::vector<const PresetRow*> halls;
    for (const PresetRow& row : preset.buildings) {
        if (row.structure().id == "building.guildhall") halls.push_back(&row);
    }
    if (halls.size() != 1 || halls[0]->count != 1 || !halls[0]->placement_conditions.empty()) {
        no_hall("expected one unconditional core guild hall in " + civilization.as_string() + " " + block.as_string());
    }
    const JsonValue* staffing = hall_staffing();
    const auto roles = halls[0]->structure().staffing;
    std::set<std::string_view> role_names;
    for (const StaffingRole& role : roles) role_names.insert(role.role);
    const bool fixed_roster =
        staffing != nullptr && staffing->at("housing_location") == JsonValue("city") &&
        role_names == std::set<std::string_view>{"cook", "bartender"} && roles.size() == 2 &&
        std::all_of(roles.begin(), roles.end(),
                    [](const StaffingRole& r) { return r.minimum == 1 && r.target == 1 && r.maximum == 1; });
    if (!fixed_roster) no_hall("guild model requires the fixed cook and bartender roster, housed in the city");

    JsonValue selected;
    JsonValue source;
    JsonValue revision;
    if (policy == nullptr) {
        const JsonValue document = guild_policy_document();
        selected = checked_policy(document.at("policies").at(civilization.as_string()), "policy.");
        source = "packaged";
        revision = document.at("revision");
    } else {
        selected = checked_policy(*policy, "policy.");
        source = "caller";
    }

    const auto req = [&](std::string_view f) { return request.at(f).as_int(); };
    const auto pol = [&](std::string_view f) { return selected.at(f).as_int(); };
    // Every product fits int64: at most 10^9 * 10^9, and (2 + 10^9) * 10^9.
    const std::int64_t resident =
        std::min(pol("max_resident_heroes"), req("population") * pol("resident_rate_numerator") / pol("resident_rate_denominator"));
    if (req("already_housed_resident_heroes") > resident) {
        PatchRefusal r = refusal::out_of_range("already_housed_resident_heroes", request.at("already_housed_resident_heroes"),
                                               0.0, static_cast<double>(resident), "integer", "");
        r.message = "housed resident heroes exceed calculated membership (" + std::to_string(resident) + ")";
        throw PatchError(std::move(r));
    }
    const std::int64_t visiting = std::min(req("visiting_heroes_requested"), pol("max_visiting_heroes"));
    const std::int64_t present = resident + visiting;
    const std::int64_t parties =
        std::min({present / pol("party_size"), pol("max_active_parties"), req("available_jobs")});
    const std::int64_t per_worker = pol("heroes_per_service_worker");
    const std::int64_t needed = (present + per_worker - 1) / per_worker;
    const std::int64_t additional = std::min(pol("max_additional_staff"), std::max<std::int64_t>(0, needed - 2));
    const std::int64_t resident_beds = resident - req("already_housed_resident_heroes");

    JsonValue plan = JsonValue::object();
    plan.set("schema", "fantasy-world-generator.hero-guild-plan");
    plan.set("schema_version", 1);
    plan.set("policy_source", source);
    plan.set("policy_revision", revision);
    plan.set("policy", selected);
    plan.set("request", request);
    JsonValue heroes = JsonValue::object();
    heroes.set("resident", resident);
    heroes.set("visiting", visiting);
    heroes.set("present", present);
    heroes.set("unserved_visitors", req("visiting_heroes_requested") - visiting);
    plan.set("heroes", std::move(heroes));
    JsonValue activity = JsonValue::object();
    activity.set("active_parties", parties);
    activity.set("active_heroes", parties * pol("party_size"));
    activity.set("inactive_heroes", present - parties * pol("party_size"));
    plan.set("activity", std::move(activity));
    JsonValue service = JsonValue::object();
    service.set("existing_workers_already_counted", 2);
    service.set("additional_workers", additional);
    service.set("total_workers", 2 + additional);
    service.set("unserved_heroes", std::max<std::int64_t>(0, present - (2 + additional) * per_worker));
    plan.set("service", std::move(service));
    JsonValue accommodation = JsonValue::object();
    accommodation.set("resident_hero_beds_needed", resident_beds);
    accommodation.set("new_staff_beds", additional);
    accommodation.set("additional_permanent_beds", resident_beds + additional);
    accommodation.set("temporary_beds_required", visiting);
    accommodation.set("temporary_bed_shortfall", std::max<std::int64_t>(0, visiting - req("available_visitor_beds")));
    plan.set("accommodation", std::move(accommodation));
    return plan;
}

}  // namespace fwg
