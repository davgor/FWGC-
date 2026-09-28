// What the C interface hands over as text, and checks coming in: the JSON forms of what the
// director answers, the UTF-8 check every incoming string passes, and the world document written
// straight into memory the caller frees.
//
// Every form unpacks its struct with a structured binding, so a field added to one of the
// director's types stops the build here until the form carries it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <streambuf>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/api/json_value.hpp"
#include "fwg/api/world.hpp"
#include "orchestrator/director.hpp"
#include "orchestrator/fwgc_director.h"
#include "utf8.hpp"

namespace orchestrator::c_api {

// `text` as a string. BadArgument, naming `what`, when it is NULL or not UTF-8.
std::string required_text(const char* text, std::string_view what);
// The same, with NULL for empty.
std::string optional_text(const char* text, std::string_view what);

// The act a name names. BadArgument for a name the contract does not have.
ActKind act_named(const std::string& name);

fwg::JsonValue packet_json(const FactPacket& packet);
fwg::JsonValue acquaintance_json(const Director::Acquaintance& acquaintance);
// [{"person", "memory", "subject", "act", "about", "times", "first_day", "last_day", "words", "source", "seq"}]
fwg::JsonValue remembered_json(const std::vector<Remembered>& memories);
// {"kept": [memories, as remembered_json], "refused": [...], "cleared": n}
fwg::JsonValue compacted_json(const Compacted& compacted);
// [{"person", "reaction", "packet"}]: the packet a joiner's reply, else null.
fwg::JsonValue bystanders_json(const std::vector<Bystander>& bystanders);
fwg::JsonValue feeling_json(const Feeling& feeling);
// {"columns": [...], "rows": [[...], ...]}: integers and reals as numbers, text as strings, NULL as
// null, and bytes that are not UTF-8 as SQL writes a blob, "x'...'".
fwg::JsonValue query_json(const QueryResult& result);
// [{"person", "event", "state"}]
fwg::JsonValue learned_json(const std::vector<Learned>& learned);
// {"knows": [...], "would_learn": [...]}
fwg::JsonValue related_json(const Related& related);
fwg::JsonValue contract_json();
// Every roster post `positions` lists, by index, with what the game shows and places them by.
fwg::JsonValue roster_json(const fwg::World& world, const std::vector<Position>& positions);

// Compact and ASCII only, refusing a NaN: how every form leaves.
std::string dumped(const fwg::JsonValue& value);

// A copy of `text` the caller frees with fwgc_free. Throws std::bad_alloc.
char* handed_over(std::string_view text);

// The world document, written straight into memory the caller frees; its length in *size.
char* handed_world(const fwg::World& world, std::size_t* size);
// "fnv1a64:" and sixteen hex digits: the FNV-1a hash of the world document, written through it
// without keeping it.
std::string world_digest(const fwg::World& world);

std::int32_t doing_code(Doing doing);
fwgc_stint stint_of(const Stint& stint);

}  // namespace orchestrator::c_api
