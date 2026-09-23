// The person-state API: the reference's terrain_person_state (pure) and
// terrain_person_api.person_state_request (the World-level call).
#include "fwg/person_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace fwg {
namespace {

// The shared refusal builders (world_time.hpp), under a name that fits on a line.
namespace refuse = ::fwg::request_errors;

constexpr std::string_view kFields[] = {"uid", "status", "reason", "day"};

std::vector<std::string> field_names() { return {std::begin(kFields), std::end(kFields)}; }

// A value as the reference's %r renders a string. Not named `quoted`: called with a
// std::string, argument-dependent lookup would find std::quoted the moment <iomanip> is
// included anywhere upstream, and pick it.
std::string repr(std::string_view text) { return "'" + std::string(text) + "'"; }

// ---------------------------------------------------------------------------
// Text measured as Python measures it: `len` counts code points, and `strip` removes what
// str.isspace calls whitespace -- the ASCII six, the separators U+001C-U+001F, and the
// Unicode spaces. A byte that starts no valid UTF-8 sequence counts as one non-space
// character, which can only make a reason longer or less blank, never shorter or blank.
// ---------------------------------------------------------------------------

bool python_space(std::uint32_t c) {
    return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000;
}

// Decodes the code point at text[i] and advances i past it.
std::uint32_t next_code_point(std::string_view text, std::size_t& i) {
    const auto lead = static_cast<unsigned char>(text[i]);
    const std::size_t length = lead < 0x80            ? 1
                               : (lead >> 5) == 0x6   ? 2
                               : (lead >> 4) == 0xE   ? 3
                               : (lead >> 3) == 0x1E  ? 4
                                                      : 0;  // a continuation byte, or 0xF8 and up
    if (length == 0 || i + length > text.size()) {
        ++i;
        return 0xFFFD;
    }
    std::uint32_t code = length == 1 ? lead : lead & (0xFFu >> (length + 1));
    for (std::size_t k = 1; k < length; ++k) {
        const auto next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        code = (code << 6) | (next & 0x3Fu);
    }
    i += length;
    return code;
}

bool blank(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        if (!python_space(next_code_point(text, i))) return false;
    }
    return true;
}

std::size_t characters(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++count) next_code_point(text, i);
    return count;
}

// ---------------------------------------------------------------------------
// The plan, one template for the four person types. `write` puts the block's word on the
// record; everything else -- read before, compare, read back -- is the predicate's.
// ---------------------------------------------------------------------------

template <typename Person, typename Write>
PersonStatePlan<Person> plan_of(const Person& record, Liveness status, LivenessBlock block, Write write) {
    PersonStatePlan<Person> out;
    out.before = liveness(record);
    if (out.before == status) {
        out.after = out.before;
        return out;
    }
    Person changed = record;
    write(changed, token(block, status));
    out.after = liveness(changed);
    if (out.after != status) {
        throw std::logic_error("liveness translation for " + std::string(to_string(block)) +
                               " does not round-trip: wrote '" + std::string(token(block, status)) + "', read " +
                               std::string(to_string(out.after)));
    }
    out.record = std::move(changed);
    return out;
}

// What a consumer would otherwise learn from behaviour, at the call that caused it. The
// reference's third note ends "; see board/backlog/VILLAIN-FALL-UNRECORDED.md", a file in the
// reference's own repository that this one does not have; the pointer is dropped.
std::vector<std::string_view> notes_for(const std::vector<PersonStateChangeReport>& applied) {
    std::vector<std::string_view> notes{
        "Nothing cascades from this call. The next tick closes an offer whose giver is gone, as it already does for "
        "a giver the world removed; everything else is untouched."};
    const bool cast = std::any_of(applied.begin(), applied.end(), [](const PersonStateChangeReport& row) {
        return row.block == LivenessBlock::Heroes || row.block == LivenessBlock::Dreads;
    });
    if (cast) {
        notes.push_back(
            "The npcs roster mirror of this person was not updated. npc_roster copies the cast into npcs.people under "
            "a prefixed uid at generation time and that copy is a snapshot; it is rebuilt at the next age boundary.");
    }
    const bool fallen = std::any_of(applied.begin(), applied.end(), [](const PersonStateChangeReport& row) {
        return row.block == LivenessBlock::Villains && row.to_status == Liveness::Gone;
    });
    if (fallen) {
        notes.push_back(
            "A villain marked gone keeps its claims, its held nodes and its absence from villains.fallen. The "
            "bookkeeping an age boundary does around a fall is not run here.");
    }
    return notes;
}

// Plans one change against the record `entry` names in `world`, writes it there when it
// applies, and says what it did.
PersonStateChangeReport apply_change(World& world, const PersonEntry& entry, const PersonChangeEntry& change) {
    PersonStateChangeReport row;
    row.uid = change.uid;
    row.block = entry.block;
    row.day = change.day;
    row.reason = change.reason;
    const auto settle = [&row](auto& slot, auto plan) {
        row.from_status = plan.before;
        row.to_status = plan.after;
        row.changed = plan.record.has_value();
        if (plan.record) slot = std::move(*plan.record);
    };
    switch (entry.block) {
        case LivenessBlock::Heroes: {
            Hero& slot = world.heroes.people.at(entry.position);
            settle(slot, person_state_plan(slot, change.status));
            break;
        }
        case LivenessBlock::Dreads: {
            HeroDread& slot = world.heroes.dreads.at(entry.position);
            settle(slot, person_state_plan(slot, change.status));
            break;
        }
        case LivenessBlock::Npcs: {
            NpcPerson& slot = world.npcs.people.at(entry.position);
            settle(slot, person_state_plan(slot, change.status));
            break;
        }
        case LivenessBlock::Villains: {
            Villain& slot = world.villains.people.at(entry.position);
            settle(slot, person_state_plan(slot, change.status));
            break;
        }
    }
    row.token = token(entry.block, row.to_status);
    return row;
}

}  // namespace

std::span<const std::string_view> person_change_fields() { return std::span<const std::string_view>(kFields); }

// ---------------------------------------------------------------------------

std::vector<PersonChangeEntry> validate_changes(std::span<const PersonChange> changes) {
    if (changes.size() > kMaxPersonChanges) {
        throw refuse::over_capacity("changes", changes.size(), kMaxPersonChanges, "person changes per request");
    }
    std::vector<PersonChangeEntry> out;
    out.reserve(changes.size());
    for (const PersonChange& item : changes) {
        // A missing field first, the first of FIELDS in declaration order -- `status` before
        // `day` -- and in the reference's shape for it: an unknown-field refusal naming the
        // field that is not there. Reproduced; see the header.
        if (!item.status) throw refuse::unknown_name("status", field_names(), "person change field");
        if (!item.day) throw refuse::unknown_name("day", field_names(), "person change field");
        if (item.uid.empty()) throw refuse::wrong_type("uid", "''", "text");
        if (std::find(kPersonStatuses.begin(), kPersonStatuses.end(), *item.status) == kPersonStatuses.end()) {
            // Named `status` because that is the field, but the vocabulary is the liveness pair
            // rather than any block's own words: a caller does not have to know which block
            // the uid will land in.
            std::vector<std::string> choices;
            for (const Liveness state : kPersonStatuses) choices.emplace_back(to_string(state));
            throw refuse::invalid_choice("status", std::to_string(static_cast<int>(*item.status)), choices);
        }
        if (blank(item.reason)) throw refuse::wrong_type("reason", repr(item.reason), "text");
        const std::size_t length = characters(item.reason);
        if (length > kMaxPersonReason) {
            throw refuse::over_capacity("reason", length, kMaxPersonReason, "characters");
        }
        const double day = *item.day;
        if (!std::isfinite(day)) throw refuse::wrong_type("day", refuse::render(day), "a finite number");
        // The reference refuses a negative day with the wrong-TYPE shape and the sentence
        // "must be a finite number". Reproduced; see the header.
        if (day < 0.0) throw refuse::wrong_type("day", refuse::render(day), "a finite number");
        out.push_back({item.uid, *item.status, item.reason, day});
    }
    return out;
}

// ---------------------------------------------------------------------------

PersonStatePlan<Hero> person_state_plan(const Hero& record, Liveness status) {
    return plan_of(record, status, LivenessBlock::Heroes,
                   [](Hero& person, std::string_view word) { person.status = std::string(word); });
}

PersonStatePlan<HeroDread> person_state_plan(const HeroDread& record, Liveness status) {
    return plan_of(record, status, LivenessBlock::Dreads,
                   [](HeroDread& dread, std::string_view word) { dread.status = std::string(word); });
}

PersonStatePlan<NpcPerson> person_state_plan(const NpcPerson& record, Liveness status) {
    // The roster keeps the published alive/dead pair as a flag: the word decides it.
    return plan_of(record, status, LivenessBlock::Npcs, [](NpcPerson& person, std::string_view word) {
        person.alive = word == present_status(LivenessBlock::Npcs);
    });
}

PersonStatePlan<Villain> person_state_plan(const Villain& record, Liveness status) {
    return plan_of(record, status, LivenessBlock::Villains,
                   [](Villain& villain, std::string_view word) { villain.status = std::string(word); });
}

// ---------------------------------------------------------------------------

std::map<std::string, std::vector<PersonEntry>> person_state_index(const World& world) {
    std::map<std::string, std::vector<PersonEntry>> found;
    const auto add = [&found](const auto& people, LivenessBlock block) {
        for (std::size_t i = 0; i < people.size(); ++i) found[people[i].uid].push_back(PersonEntry{block, i});
    };
    // A failed package carries no people: zero present people and no package are different
    // facts. The villain block has no block-level status to fail on.
    if (world.has_heroes && world.heroes.status == "ok") {
        add(world.heroes.people, LivenessBlock::Heroes);
        add(world.heroes.dreads, LivenessBlock::Dreads);
    }
    if (world.has_npcs && world.npcs.ok) add(world.npcs.people, LivenessBlock::Npcs);
    if (world.has_villains) add(world.villains.people, LivenessBlock::Villains);
    return found;
}

PersonStateResult person_state_request(const World& world, const PersonStateRequest& request) {
    // Everything judgeable before the world is read.
    if (request.api_version != kPersonApiVersion) {
        throw refuse::unsupported_api(request.api_version, kPersonApiVersion);
    }
    const std::vector<PersonChangeEntry> changes = validate_changes(request.changes);

    // The same world gate the tick uses, for the reason the quest route uses it.
    validate_time_world(world);

    const std::map<std::string, std::vector<PersonEntry>> known = person_state_index(world);
    if (known.empty()) {
        throw refuse::missing_block("people",
                                    "This world carries no person lists to change. heroes, npcs and villains are "
                                    "written at generation; a world whose cast packages failed has none.");
    }

    // Resolve everything first. After this loop no refusal remains, so no path exists on which
    // a partly written world was ever built.
    std::vector<std::pair<const PersonChangeEntry*, PersonEntry>> resolved;
    resolved.reserve(changes.size());
    for (const PersonChangeEntry& change : changes) {
        const auto where = known.find(change.uid);
        if (where == known.end()) {
            std::vector<std::string> uids;
            uids.reserve(known.size());
            for (const auto& [uid, entries] : known) uids.push_back(uid);
            throw refuse::unknown_name(change.uid, uids, "person");
        }
        if (where->second.size() > 1) {
            std::vector<std::string> blocks;
            for (const PersonEntry& entry : where->second) blocks.emplace_back(to_string(entry.block));
            std::sort(blocks.begin(), blocks.end());
            std::string listed;
            for (std::size_t i = 0; i < blocks.size(); ++i) listed += (i == 0 ? "" : ", ") + blocks[i];
            throw refuse::refused_by_world("uid", change.uid,
                                           "uid " + change.uid + " names a person in more than one block (" + listed +
                                               "), so this call cannot tell which one you mean.",
                                           {});
        }
        resolved.emplace_back(&change, where->second.front());
    }

    // Applied in caller order to a private copy, so a later change to the same person sees
    // the earlier one, as the reference's does.
    PersonStateResult result{world, {}};
    std::vector<PersonStateChangeReport> applied;
    PersonOperation operation;
    for (const auto& [change, entry] : resolved) {
        PersonStateChangeReport row = apply_change(result.world, entry, *change);
        if (row.changed) {
            applied.push_back(row);
            operation.changes.push_back(*change);
        }
        result.report.changes.push_back(std::move(row));
    }
    result.report.applied = static_cast<int>(applied.size());
    result.report.notes = notes_for(applied);
    // Nothing moved, so nothing is recorded: the copy is the caller's world, unchanged.
    if (!applied.empty()) result.world.history.operations.push_back(std::move(operation));
    return result;
}

}  // namespace fwg
