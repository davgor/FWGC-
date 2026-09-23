// Hero guild planning, version 1: the reference's cases, which are independent examples
// worked by hand from the documented formulas.
#include "check.hpp"

#include <string>

#include "fwg/capabilities.hpp"
#include "fwg/city_plan.hpp"
#include "fwg/hero_guild.hpp"
#include "product_fixtures.hpp"

using namespace fwg;

namespace {

JsonValue request(const char* overrides = "{}") {
    JsonValue r = parse_json(R"({"civilization_id": "human_heartland", "city_block": "small_city", "population": 100,
        "visiting_heroes_requested": 0, "available_jobs": 0, "already_housed_resident_heroes": 0, "available_visitor_beds": 0})");
    const JsonValue changes = parse_json(overrides);
    for (const auto& [key, value] : changes.as_object()) r.set(key, value);
    return r;
}

JsonValue policy(const char* overrides = "{}") {
    JsonValue p = parse_json(R"({"resident_rate_numerator": 1, "resident_rate_denominator": 10, "max_resident_heroes": 100,
        "max_visiting_heroes": 100, "party_size": 4, "max_active_parties": 10, "heroes_per_service_worker": 5,
        "max_additional_staff": 10})");
    const JsonValue changes = parse_json(overrides);
    for (const auto& [key, value] : changes.as_object()) p.set(key, value);
    return p;
}

std::string section(const JsonValue& plan, const char* key) { return dump_json(plan.at(key)); }

JsonValue plan_with(const JsonValue& r, const JsonValue& p) { return plan_hero_guild(r, &p); }

}  // namespace

TEST(guild_defaults_invent_no_heroes_and_no_worker_beds) {
    require_capabilities(parse_json(R"({"capability_version": 1, "requires": {"hero_guild_planning": 1}})"));
    for (const char* population : {"0", "1", "1000000"}) {
        const JsonValue plan = plan_hero_guild(request((std::string(R"({"population": )") + population + "}").c_str()));
        check::equal(section(plan, "heroes"), std::string(R"({"resident": 0, "visiting": 0, "present": 0, "unserved_visitors": 0})"),
                     std::string("no heroes at population ") + population);
        check::equal(plan.at("accommodation").at("additional_permanent_beds").as_int(), std::int64_t{0}, "no permanent beds");
        check::equal(plan.at("service").at("existing_workers_already_counted").as_int(), std::int64_t{2}, "the hall's two");
        check::equal(plan.at("service").at("additional_workers").as_int(), std::int64_t{0}, "no extra staff");
        check::equal(plan.at("policy_source").as_string(), std::string("packaged"), "the packaged policy");
        check::equal(plan.at("policy_revision").as_int(), std::int64_t{1}, "its revision");
    }
}

TEST(guild_independent_demographic_activity_service_and_bed_example) {
    const JsonValue plan = plan_with(request(R"({"population": 105, "visiting_heroes_requested": 7, "available_jobs": 3,
        "already_housed_resident_heroes": 4, "available_visitor_beds": 2})"), policy());
    check::equal(section(plan, "heroes"), std::string(R"({"resident": 10, "visiting": 7, "present": 17, "unserved_visitors": 0})"), "heroes");
    check::equal(section(plan, "activity"), std::string(R"({"active_parties": 3, "active_heroes": 12, "inactive_heroes": 5})"), "activity");
    check::equal(section(plan, "service"),
                 std::string(R"({"existing_workers_already_counted": 2, "additional_workers": 2, "total_workers": 4, "unserved_heroes": 0})"),
                 "service");
    check::equal(section(plan, "accommodation"),
                 std::string(R"({"resident_hero_beds_needed": 6, "new_staff_beds": 2, "additional_permanent_beds": 8, "temporary_beds_required": 7, "temporary_bed_shortfall": 5})"),
                 "accommodation");
    check::equal(plan.at("policy_source").as_string(), std::string("caller"), "a caller policy");
    check::is_true(plan.at("policy_revision").is_null(), "carries no revision");
    std::string keys;
    for (const auto& m : plan.as_object()) keys += m.first + " ";
    check::equal(keys, std::string("schema schema_version policy_source policy_revision policy request heroes activity service accommodation "),
                 "the plan's keys, in order");
}

TEST(guild_small_city_and_sole_capital_share_rates_and_caps_report_unserved_demand) {
    const JsonValue small = plan_with(request(R"({"population": 9})"), policy());
    const JsonValue capital = plan_with(request(R"({"population": 9, "city_block": "capital_city"})"), policy());
    check::equal(section(small, "heroes"), section(capital, "heroes"), "a capital title has no multiplier");
    check::equal(capital.at("heroes").at("resident").as_int(), std::int64_t{0}, "9 // 10 is 0");
    const JsonValue capped = plan_with(request(R"({"population": 100, "visiting_heroes_requested": 9, "available_jobs": 100})"), policy(R"({"max_resident_heroes": 3, "max_visiting_heroes": 6, "max_active_parties": 1,
                                                 "heroes_per_service_worker": 2, "max_additional_staff": 1})"));
    check::equal(section(capped, "heroes"), std::string(R"({"resident": 3, "visiting": 6, "present": 9, "unserved_visitors": 3})"), "caps");
    check::equal(section(capped, "activity"), std::string(R"({"active_parties": 1, "active_heroes": 4, "inactive_heroes": 5})"), "parties");
    check::equal(capped.at("service").at("unserved_heroes").as_int(), std::int64_t{3}, "service shortfall");
    check::equal(capped.at("accommodation").at("additional_permanent_beds").as_int(), std::int64_t{4}, "beds");
    check::equal(capped.at("accommodation").at("temporary_beds_required").as_int(), std::int64_t{6}, "visitors' beds");
    const JsonValue large = plan_with(request(R"({"population": 999999999})"), policy(R"({"resident_rate_numerator": 999999998, "resident_rate_denominator": 999999999,
                                                "max_resident_heroes": 1000000000})"));
    check::equal(large.at("heroes").at("resident").as_int(), std::int64_t{999999998}, "integer rational arithmetic at the bounds");
}

TEST(guild_inputs_and_policies_are_strict) {
    for (const char* bad : {"true", "1.0", "\"1\"", "-1", "1000000001"}) {
        check::throws([&] { plan_hero_guild(request((std::string(R"({"population": )") + bad + "}").c_str())); },
                      std::string("population ") + bad);
    }
    for (const char* change : {R"({"civilization_id": "unknown"})", R"({"city_block": "village"})", R"({"extra": 0})",
                               R"({"already_housed_resident_heroes": 1})"}) {
        check::throws([&] { plan_hero_guild(request(change)); }, std::string("request ") + change);
    }
    for (const char* change : {R"({"party_size": 0})", R"({"resident_rate_denominator": 0})", R"({"resident_rate_numerator": 11})",
                               R"({"max_additional_staff": true})", R"({"extra": 0})"}) {
        const JsonValue bad = policy(change);
        check::throws([&] { plan_hero_guild(request(), &bad); }, std::string("policy ") + change);
    }
    const PatchRefusal r = product_test::refusal_of([] { plan_hero_guild(request(R"({"city_block": "village"})")); });
    check::equal(r.field, std::string("city_block"), "the refusal names the field");
    JsonValue document = guild_policy_document();
    document.set("revision", 999);
    check::equal(guild_policy_document().at("revision").as_int(), std::int64_t{1}, "caller-owned policies");
}

TEST(guild_every_civilization_and_city_block_resolves_its_core_hall) {
    check::equal(guild_policy_document().at("policies").size(), civilization_presets().size(), "one policy per civilization");
    for (const CivilizationPresets& presets : civilization_presets()) {
        for (const char* block : {"small_city", "medium_city", "capital_city"}) {
            JsonValue r = request();
            r.set("civilization_id", std::string(presets.id));
            r.set("city_block", block);
            check::equal(plan_hero_guild(r).at("service").at("existing_workers_already_counted").as_int(), std::int64_t{2},
                         std::string(presets.id) + " " + block);
        }
    }
}
