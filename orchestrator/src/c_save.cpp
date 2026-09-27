#include "c_save.hpp"

#include <initializer_list>
#include <limits>
#include <variant>

#include "c_guard.hpp"
#include "c_json.hpp"
#include "fwg/api/world_time.hpp"

namespace orchestrator::c_api {

namespace {

// Every input kind has its key here, and replay() its call. A new one stops the build until it
// has both, and its fwgc_ function and a test.
static_assert(std::variant_size_v<Input> == 9,
              "a new Input kind needs its save key (c_save.cpp), its fwgc_ function (c_api.cpp) and a test "
              "(tests/test_c_api.cpp)");

template <class... Visits>
struct Overloaded : Visits... {
    using Visits::operator()...;
};
template <class... Visits>
Overloaded(Visits...) -> Overloaded<Visits...>;

fwg::JsonValue keyed(std::string_view key, fwg::JsonValue body) {
    fwg::JsonValue out = fwg::JsonValue::object();
    out.set(key, std::move(body));
    return out;
}

fwg::JsonValue input_json(const Input& input) {
    return std::visit(
        Overloaded{
            [](const Advance& advance) {
                const auto& [days] = advance;
                fwg::JsonValue body = fwg::JsonValue::object();
                body.set("days", days);
                return keyed("advance", std::move(body));
            },
            [](const Say& say) {
                const auto& [speaker, act] = say;
                const auto& [kind, target] = act;
                fwg::JsonValue body = fwg::JsonValue::object();
                body.set("speaker", speaker);
                body.set("act", orchestrator::to_string(kind));
                body.set("target", target);
                return keyed("say", std::move(body));
            },
            [](const Resolve& resolve) {
                const auto& [quest_id, completed] = resolve;
                fwg::JsonValue body = fwg::JsonValue::object();
                body.set("quest_id", quest_id);
                body.set("completed", completed);
                return keyed("resolve", std::move(body));
            },
            [](const Hold& hold) {
                const auto& [person, minutes] = hold;
                fwg::JsonValue body = fwg::JsonValue::object();
                body.set("person", person);
                body.set("minutes", minutes);
                return keyed("hold", std::move(body));
            },
            [](const Fright& fright) {
                const auto& [person, level] = fright;
                fwg::JsonValue body = fwg::JsonValue::object();
                body.set("person", person);
                body.set("level", level);
                return keyed("fright", std::move(body));
            },
            [](const Gather& gather) {
                const auto& [people] = gather;
                fwg::JsonValue names = fwg::JsonValue::array();
                for (const std::string& name : people) names.push_back(name);
                fwg::JsonValue body = fwg::JsonValue::object();
                body.set("people", std::move(names));
                return keyed("gather", std::move(body));
            },
            [](const Overhear& overhear) {
                const auto& [people] = overhear;
                fwg::JsonValue names = fwg::JsonValue::array();
                for (const std::string& name : people) names.push_back(name);
                fwg::JsonValue body = fwg::JsonValue::object();
                body.set("people", std::move(names));
                return keyed("overhear", std::move(body));
            },
            [](const Share& share) {
                const auto& [people] = share;
                fwg::JsonValue names = fwg::JsonValue::array();
                for (const std::string& name : people) names.push_back(name);
                fwg::JsonValue body = fwg::JsonValue::object();
                body.set("people", std::move(names));
                return keyed("share", std::move(body));
            },
            [](const Report& report) {
                const auto& [site, outcome, by] = report;
                fwg::JsonValue body = fwg::JsonValue::object();
                body.set("site", site);
                body.set("outcome", outcome);
                body.set("by", by);
                return keyed("report", std::move(body));
            },
        },
        input);
}

// ---- reading, strictly ------------------------------------------------------------------------

[[noreturn]] void refuse(const std::string& why) { throw Incompatible("not a save this library can restore: " + why); }

// `object` holds exactly `keys`, every one of them.
void exactly(const fwg::JsonValue& object, std::initializer_list<std::string_view> keys, std::string_view what) {
    if (!object.is_object()) refuse(std::string(what) + " is not an object");
    for (const fwg::JsonValue::Member& member : object.as_object()) {
        bool known = false;
        for (const std::string_view k : keys) known = known || member.first == k;
        if (!known) refuse(std::string(what) + " has a key it should not: '" + member.first + "'");
    }
    for (const std::string_view k : keys) {
        if (!object.contains(k)) refuse(std::string(what) + " lacks '" + std::string(k) + "'");
    }
}

std::string text_at(const fwg::JsonValue& object, std::string_view key, std::string_view what) {
    const fwg::JsonValue& value = object.at(key);
    if (!value.is_string() || !is_utf8(value.as_string())) refuse(std::string(what) + "." + std::string(key) + " is not text");
    return value.as_string();
}

double number_at(const fwg::JsonValue& object, std::string_view key, std::string_view what) {
    const std::optional<double> value = object.at(key).to_double();
    if (!value) refuse(std::string(what) + "." + std::string(key) + " is not a number");
    return *value;
}

std::int64_t integer_at(const fwg::JsonValue& object, std::string_view key, std::string_view what) {
    const fwg::JsonValue& value = object.at(key);
    if (!value.is_int64()) refuse(std::string(what) + "." + std::string(key) + " is not an integer");
    return value.as_int();
}

int small_integer_at(const fwg::JsonValue& object, std::string_view key, std::string_view what) {
    const std::int64_t value = integer_at(object, key, what);
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        refuse(std::string(what) + "." + std::string(key) + " is out of range");
    return static_cast<int>(value);
}

// A list of uids, strictly.
std::vector<std::string> people_at(const fwg::JsonValue& body, std::string_view what) {
    const fwg::JsonValue& people = body.at("people");
    if (!people.is_array()) refuse(std::string(what) + ".people is not a list");
    std::vector<std::string> out;
    for (const fwg::JsonValue& person : people.as_array()) {
        if (!person.is_string() || !is_utf8(person.as_string())) refuse(std::string(what) + ".people holds something not text");
        out.push_back(person.as_string());
    }
    return out;
}

Input input_from(const fwg::JsonValue& item) {
    if (!item.is_object() || item.size() != 1) refuse("an input is not an object with one key");
    const auto& [kind, body] = item.as_object().front();
    if (kind == "advance") {
        exactly(body, {"days"}, "advance");
        return Advance{number_at(body, "days", kind)};
    }
    if (kind == "say") {
        exactly(body, {"speaker", "act", "target"}, "say");
        const std::optional<ActKind> act = act_kind(text_at(body, "act", kind));
        if (!act) refuse("say.act names no act this contract has");
        return Say{text_at(body, "speaker", kind), DialogueAct{*act, text_at(body, "target", kind)}};
    }
    if (kind == "resolve") {
        exactly(body, {"quest_id", "completed"}, "resolve");
        if (!body.at("completed").is_bool()) refuse("resolve.completed is not true or false");
        return Resolve{text_at(body, "quest_id", kind), body.at("completed").as_bool()};
    }
    if (kind == "hold") {
        exactly(body, {"person", "minutes"}, "hold");
        return Hold{text_at(body, "person", kind), number_at(body, "minutes", kind)};
    }
    if (kind == "fright") {
        exactly(body, {"person", "level"}, "fright");
        return Fright{text_at(body, "person", kind), small_integer_at(body, "level", kind)};
    }
    if (kind == "gather") {
        exactly(body, {"people"}, "gather");
        return Gather{people_at(body, kind)};
    }
    if (kind == "overhear") {
        exactly(body, {"people"}, "overhear");
        return Overhear{people_at(body, kind)};
    }
    if (kind == "share") {
        exactly(body, {"people"}, "share");
        return Share{people_at(body, kind)};
    }
    if (kind == "report") {
        exactly(body, {"site", "outcome", "by"}, "report");
        return Report{text_at(body, "site", kind), text_at(body, "outcome", kind), text_at(body, "by", kind)};
    }
    refuse("an input of a kind this library does not know: '" + kind + "'");
}

}  // namespace

std::string save_text(const fwg::JsonValue& request, const std::string& start_digest, const Director& director) {
    fwg::JsonValue inputs = fwg::JsonValue::array();
    for (const Input& input : director.inputs()) inputs.push_back(input_json(input));
    fwg::JsonValue check = fwg::JsonValue::object();
    check.set("day", clock_day(director));
    check.set("operations", operation_count(director));
    fwg::JsonValue root = fwg::JsonValue::object();
    root.set("schema", kSaveSchema);
    root.set("version", kSaveVersion);
    root.set("contract_version", kContractVersion);
    root.set("request", request);
    root.set("start_digest", start_digest);
    root.set("inputs", std::move(inputs));
    root.set("check", std::move(check));
    return dumped(root);
}

Save read_save(std::string_view text) {
    fwg::JsonValue root;
    try {
        root = fwg::parse_json(text);
    } catch (const fwg::JsonError& e) {
        refuse(std::string("it is not JSON (") + e.what() + ")");
    }
    exactly(root, {"schema", "version", "contract_version", "request", "start_digest", "inputs", "check"}, "the save");
    if (text_at(root, "schema", "the save") != kSaveSchema) refuse("its schema is not " + std::string(kSaveSchema));
    if (integer_at(root, "version", "the save") != kSaveVersion)
        refuse("it is version " + std::to_string(integer_at(root, "version", "the save")) + ", and this library reads version " +
               std::to_string(kSaveVersion));
    if (integer_at(root, "contract_version", "the save") != kContractVersion)
        refuse("it was made under contract version " + std::to_string(integer_at(root, "contract_version", "the save")) +
               ", and this library speaks version " + std::to_string(kContractVersion));
    Save save;
    save.request = root.at("request");
    if (!save.request.is_object()) refuse("its request is not an object");
    save.start_digest = text_at(root, "start_digest", "the save");
    const fwg::JsonValue& inputs = root.at("inputs");
    if (!inputs.is_array()) refuse("its inputs are not a list");
    for (const fwg::JsonValue& item : inputs.as_array()) save.inputs.push_back(input_from(item));
    const fwg::JsonValue& check = root.at("check");
    exactly(check, {"day", "operations"}, "the save's check");
    save.day = number_at(check, "day", "check");
    save.operations = integer_at(check, "operations", "check");
    return save;
}

double clock_day(const Director& director) { return fwg::world_clock_of(director.world()).day; }

std::int64_t operation_count(const Director& director) {
    return static_cast<std::int64_t>(director.world().history.operations.size());
}

}  // namespace orchestrator::c_api
