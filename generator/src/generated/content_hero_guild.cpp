// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/hero-guild-policies.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/product_content.hpp"

#include <string>

namespace fwg {

namespace {

// the hero guild policies: 2823 bytes of JSON in 1 pieces.
constexpr std::string_view k_hero_guild_policies_json[] = {
    R"fwgjson({"schema":"fantasy-world-generator.hero-guild-policies","schema_version":1,"revision":1,"policies":{"human_maritime":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"human_desert":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"human_cold":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"human_large_island":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"human_rainforest":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"human_heartland":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"dwarf":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"elf":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"gnome":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"tidekin":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"hill_dwarf":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20},"frosthold_dwarf":{"resident_rate_numerator":0,"resident_rate_denominator":1000,"max_resident_heroes":1000,"max_visiting_heroes":1000,"party_size":4,"max_active_parties":25,"heroes_per_service_worker":20,"max_additional_staff":20}}})fwgjson",
};

}  // namespace

std::string_view hero_guild_policies_json() {
    static const std::string joined = [] {
        std::string text;
        text.reserve(2823);
        for (const std::string_view piece : k_hero_guild_policies_json) text.append(piece);
        return text;
    }();
    return joined;
}

}  // namespace fwg
