#include "memory.hpp"

#include <charconv>

namespace communicator {

namespace {

int number(const std::string* text) {
    int out = 0;
    if (text) std::from_chars(text->data(), text->data() + text->size(), out);
    return out;
}

}  // namespace

Remembered remembered(const orchestrator::FactPacket& packet) {
    Remembered out;
    for (const orchestrator::Fact& fact : packet.facts) {
        if (fact.kind != orchestrator::FactKind::Memory) continue;
        const auto text = [&](std::string_view key) {
            const std::string* value = fact.field(key);
            return value ? *value : std::string();
        };
        out.again = text("met") == "again";
        out.times = number(fact.field("times"));
        out.since = text("since");
        if (!text("mood").empty()) out.mood = text("mood");
        out.work = text("work");
        out.beside = text("beside") == "yes";
        out.work_news = text("work_news") == "yes";
        out.asked_before = text("asked_before");
        out.turn = number(fact.field("turn"));
        out.repeat = text("repeat") == "yes";
        out.heard = number(fact.field("heard"));
        out.exposed = text("exposed") == "yes";
        if (!text("feeling").empty()) out.feeling = text("feeling");
        if (!text("resting").empty()) out.resting = text("resting");
        out.eased = text("eased") == "yes";
        out.joined = text("joined") == "yes";
        break;
    }
    return out;
}

std::vector<Recall> recalls(const orchestrator::FactPacket& packet) {
    std::vector<Recall> out;
    for (const orchestrator::Fact& fact : packet.facts) {
        if (fact.kind != orchestrator::FactKind::Recall) continue;
        const auto text = [&](std::string_view key) {
            const std::string* value = fact.field(key);
            return value ? *value : std::string();
        };
        Recall r;
        r.subject = fact.subject;
        r.memory = text("memory");
        r.act = text("act");
        r.about = text("about");
        r.times = number(fact.field("times"));
        r.since = text("since");
        r.first = text("first");
        r.words = text("words");
        out.push_back(std::move(r));
    }
    return out;
}

const Recall* recall_of(const std::vector<Recall>& all, std::string_view memory) {
    for (const Recall& r : all)
        if (r.memory == memory) return &r;
    return nullptr;
}

std::string since_words(const std::string& since) {
    if (since == "today") return "earlier today";
    if (since == "yesterday") return "yesterday";
    if (since == "days") return "a few days ago";
    if (since == "weeks") return "some weeks ago";
    if (since == "months") return "some months ago";
    if (since == "years") return "years ago";
    return {};
}

}  // namespace communicator
