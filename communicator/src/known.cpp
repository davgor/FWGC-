#include "communicator/known.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

#include "communicator/error.hpp"
#include "text.hpp"

namespace communicator {

namespace {

using orchestrator::Fact;
using orchestrator::FactKind;
using text::one_line;

constexpr std::array<std::string_view, 4> kKindNames = {"place", "person", "offer", "held"};
static_assert(kKindNames.size() == static_cast<std::size_t>(Kind::Held) + 1, "one name per kind");

std::string field_or_empty(const Fact& fact, std::string_view key) {
    const std::string* value = fact.field(key);
    return value ? one_line(*value) : std::string();
}

bool same(const Referent& a, const Referent& b) { return a.kind == b.kind && a.id == b.id; }

}  // namespace

std::string_view to_string(Kind kind) { return kKindNames[static_cast<std::size_t>(kind)]; }

std::string label_of(const Fact& fact) {
    switch (fact.kind) {
        case FactKind::Place: {
            const std::string name = field_or_empty(fact, "name");
            if (!name.empty()) return name;
            // A hamlet or a fortress has no name of its own: it is known by the city it belongs to.
            const std::string kind = field_or_empty(fact, "kind");
            const std::string of = field_or_empty(fact, "of");
            return kind.empty() || of.empty() ? std::string() : "the " + text::spaced(kind) + " of " + of;
        }
        case FactKind::Person:
            return field_or_empty(fact, "name");
        case FactKind::QuestOffer: {
            std::string what = field_or_empty(fact, "purpose");
            if (what.empty()) {
                const std::string verb = field_or_empty(fact, "verb");
                const std::string where = field_or_empty(fact, "where");
                what = verb.empty() || where.empty() ? std::string() : verb + " " + where;
            }
            if (what.empty()) return {};
            const std::string giver = field_or_empty(fact, "giver");
            return giver.empty() ? what : giver + ": " + what;
        }
        case FactKind::QuestState:
        case FactKind::Danger:
        case FactKind::Reason:
        case FactKind::Memory:
        case FactKind::Chat:
        case FactKind::Safety:
        case FactKind::Reveal:
            return {};
    }
    return {};
}

void Known::learn(const orchestrator::FactPacket& packet) {
    if (packet.contract_version != orchestrator::kContractVersion)
        throw Error("a packet of contract version " + std::to_string(packet.contract_version) + "; this reads version " +
                    std::to_string(orchestrator::kContractVersion));

    std::vector<Referent> fresh;
    std::vector<Referent> forget;  // taken offers, abandoned work
    const auto add = [&](Kind kind, const std::string& id, std::string label) {
        if (id.empty() || label.empty()) return;
        Referent r{kind, id, std::move(label)};
        if (std::none_of(fresh.begin(), fresh.end(), [&](const Referent& f) { return same(f, r); })) fresh.push_back(std::move(r));
    };
    const auto offer_label = [&](const std::string& id) -> std::string {
        for (const std::vector<Referent>* list : {&fresh, &referents_})
            for (const Referent& r : *list)
                if (r.kind == Kind::Offer && r.id == id) return r.label;
        return {};
    };

    add(Kind::Person, packet.persona.uid, one_line(packet.persona.name));
    for (const Fact& fact : packet.facts) {
        switch (fact.kind) {
            case FactKind::Place:
                add(Kind::Place, fact.subject, label_of(fact));
                break;
            case FactKind::Person:
                add(Kind::Person, fact.subject, label_of(fact));
                break;
            case FactKind::QuestOffer:
                add(Kind::Offer, fact.subject, label_of(fact));
                break;
            case FactKind::QuestState: {
                const std::string state = field_or_empty(fact, "state");
                if (state == "taken") {
                    // Taken work is named as it was offered; work never offered in a packet
                    // cannot be named at all.
                    add(Kind::Held, fact.subject, offer_label(fact.subject));
                    forget.push_back({Kind::Offer, fact.subject, {}});
                } else if (state == "abandoned") {
                    forget.push_back({Kind::Held, fact.subject, {}});
                }
                break;
            }
            case FactKind::Danger:
            case FactKind::Reason:
            case FactKind::Memory:
            case FactKind::Chat:
            case FactKind::Safety:
            case FactKind::Reveal:
                break;
        }
    }

    std::vector<Referent> next = fresh;
    for (Referent& old : referents_) {
        const auto matches = [&](const Referent& r) { return same(r, old); };
        if (std::any_of(fresh.begin(), fresh.end(), matches) || std::any_of(forget.begin(), forget.end(), matches)) continue;
        next.push_back(std::move(old));
    }
    next.erase(std::remove_if(next.begin(), next.end(),
                              [&](const Referent& r) {
                                  return std::any_of(forget.begin(), forget.end(), [&](const Referent& f) { return same(f, r); });
                              }),
               next.end());

    // Keep the latest kKeptPerKind of each kind.
    std::array<std::size_t, kKindNames.size()> counted{};
    referents_.clear();
    for (Referent& r : next)
        if (counted[static_cast<std::size_t>(r.kind)]++ < kKeptPerKind) referents_.push_back(std::move(r));
}

std::vector<Referent> Known::nameable(Kind kind, std::size_t limit) const {
    std::vector<Referent> out;
    std::set<std::string> labels;
    for (const Referent& r : referents_) {
        if (out.size() == limit) break;
        if (r.kind == kind && labels.insert(r.label).second) out.push_back(r);
    }
    return out;
}

std::optional<std::string> Known::resolve(Kind kind, std::string_view label) const {
    for (const Referent& r : referents_)
        if (r.kind == kind && r.label == label) return r.id;
    return std::nullopt;
}

}  // namespace communicator
