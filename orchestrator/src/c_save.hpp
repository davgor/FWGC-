// A save, as the C interface writes and reads it: the generator request the world was made from,
// the digest of that world as made, every input since, and the clock and the count of the
// world's logged operations after them -- so a save that would replay onto a different world, or
// replay differently, is refused instead of restored.
//
//   {"schema": "fwgc.save", "version": 1, "contract_version": 10, "request": {...},
//    "start_digest": "fnv1a64:...", "inputs": [{"advance": {"days": 30.0}}, {"say": {"speaker":
//    ..., "act": "greet", "target": ""}}, {"resolve": {"quest_id": ..., "completed": true}},
//    {"hold": {"person": ..., "minutes": 20.0}}, {"fright": {"person": ..., "level": 2}},
//    {"gather": {"people": [...]}}, {"overhear": {"people": [...]}}],
//    "check": {"day": ..., "operations": ...}}
//
// Written compact and ASCII only; every number reads back as the same double.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/api/json_value.hpp"
#include "orchestrator/director.hpp"

namespace orchestrator::c_api {

inline constexpr std::string_view kSaveSchema = "fwgc.save";
inline constexpr int kSaveVersion = 1;

struct Save {
    fwg::JsonValue request;
    std::string start_digest;
    std::vector<Input> inputs;
    double day = 0.0;
    std::int64_t operations = 0;
};

// The save of `director`, whose world was made from `request` and came out as `start_digest`.
std::string save_text(const fwg::JsonValue& request, const std::string& start_digest, const Director& director);

// A save read back. Throws Incompatible for anything save_text at this version would not write:
// text that is not JSON, another schema or version, a key or an input kind or an act it does
// not know, a value of the wrong type.
Save read_save(std::string_view text);

// What a save's check holds: the clock, and how many operations the world has logged.
double clock_day(const Director& director);
std::int64_t operation_count(const Director& director);

}  // namespace orchestrator::c_api
