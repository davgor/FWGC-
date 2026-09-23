// What the world document publishes about the peoples, beyond where they settled.
//
// Four top-level blocks come out of the civilization registry rather than out of a stage:
//
//   * `population`          -- the profile the world was asked for (`mixed`, or one people);
//   * `population_profiles` -- every profile's id and name, the request form's choices;
//   * `peoples`             -- the full profile record of every people that competed for
//                              ground, from stage 10;
//   * `civilizations`       -- the reference's civilization_report: registry and heritage
//                              identities, the classification rule, the parent races and one
//                              row per entity with its heritage, city count and capital.
//
// A PROFILE RECORD is the reference's validated profile: every trait, `id`, the
// `civilization` identity, `schema_version` 4 and a `definition_hash`. The hash is SHA-256
// over this port's own canonical form of the record -- sorted keys, compact separators,
// written by JsonWriter -- not over the reference's (`json.dumps(sort_keys=True)` with its
// default separators and ensure_ascii). The two canonicalisations disagree on spacing, on
// `18` against `18.0` and on non-ASCII, so the digests differ from the reference's by
// construction; what a digest is FOR -- noticing that a profile changed -- is unchanged,
// and this port's registry identity takes the same position (civilizations.hpp).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "fwg/civilizations.hpp"
#include "fwg/entities.hpp"
#include "fwg/json.hpp"

namespace fwg {

inline constexpr int kProfileSchemaVersion = 4;
inline constexpr int kCivilizationReportVersion = 3;

// The registry's classification prose, republished verbatim. Generated.
std::string_view capital_scope();

struct PresentationDefault {
    std::string_view key;  // e.g. "outpost_wall_color_rgb"
    Rgb color;
};
// The registry `defaults` whose keys end in `_color_rgb`, in authored order. Generated.
std::span<const PresentationDefault> presentation_defaults();

// Whether this entity's outposts take the override colours; nullopt for an unknown id.
// Display only, like the rest of an entity's presentation. Generated.
std::optional<bool> entity_outpost_colors(std::string_view entity_id);

// A habitat rule as the registry authors it: `{}` for everywhere, `{"all": [...]}` /
// `{"any": [...]}` for a combinator, and a leaf's field with the bounds it sets. A leaf on
// a boolean-valued field (largest_landmass) writes its `equals` as a JSON boolean: the
// port evaluates it as 0/1, and `false` is how the rule is authored.
void write_habitat(JsonWriter& json, std::span<const HabitatNode> nodes, const HabitatPools& pools,
                   std::uint32_t index = 0);

// The full profile record, keys sorted, `definition_hash` included.
void write_profile_record(JsonWriter& json, const CivilizationProfile& profile);

// SHA-256 hex over the record without its hash, in the canonical form above.
std::string profile_definition_hash(const CivilizationProfile& profile);

}  // namespace fwg
