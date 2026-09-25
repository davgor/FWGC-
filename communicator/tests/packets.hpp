// Fact packets as the director writes them, built by hand: this component never sees a world.
// Their text is taken from real packets (seed 42, size 33, a month old), so the tests read what
// the voice and the interpreter will actually be handed. The ids are the director's too.
#pragma once

#include <string>
#include <vector>

#include "orchestrator/contract.hpp"

namespace packets {

using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;
using orchestrator::Persona;

inline Persona castellan() {
    Persona p;
    p.uid = "hero-castellan-fortress-node-178";
    p.name = "Selbren";
    p.role = "castellan";
    p.place = "Wenweg (Age 2)";
    p.manner = "The watch rota is sacred.";
    p.voice = "Complete sentences, titles and forms of address, precedent and oaths cited when pressed. Direct and "
              "self-interested without malice.";
    p.toward_player = "Offers a fair trade and expects the same. Proves the player's standing before trusting them with "
                      "anything that matters.";
    p.line = "Third night this month it's tested the ward. You want through? You're coming back with a message.";
    p.never = {"leave the post", "break a sworn word", "act outside the office they hold", "cruelty for its own sake"};
    // As the director gave it for this uid in that world (2026-09-24): a warden, lawful neutral,
    // with one face.
    p.voice_key = "warden/LN";
    return p;
}

// A roster person: no card, only a name, a post and a place.
inline Persona gate_captain() {
    Persona p;
    p.uid = "npc-fortress-node-178-barbican-0";
    p.name = "Fersel";
    p.role = "gate_captain";
    p.place = "the fortress of Wenweg (Age 2)";
    return p;
}

inline FactPacket packet(Persona persona, ActKind answering, Outcome outcome, std::vector<Fact> facts = {}) {
    FactPacket p;
    p.speaker = persona.uid;
    p.answering = answering;
    p.outcome = outcome;
    p.persona = std::move(persona);
    p.facts = std::move(facts);
    return p;
}

inline FactPacket surroundings() {
    return packet(castellan(), ActKind::AskSurroundings, Outcome::Answered,
                  {
                      {FactKind::Place, "surface-city-2-212-human_cold",
                       {{"kind", "city"}, {"name", "Wenweg (Age 2)"}, {"class", "small"}, {"people", "human_cold"}, {"distance", "here"}}},
                      {FactKind::Place, "keyloc-waystone-road_waystones-rsurface-city-2-142-human_cold-surface-city-2-212-human_cold-19800",
                       {{"kind", "waystone"}, {"name", "the ninth stone on the Linbrig road"}, {"family", "wayside"}, {"state", "abandoned"},
                        {"threat", "4"}, {"occupant", "monsters"}, {"distance", "2.5 km"}}},
                      {FactKind::Place, "hamlet-39", {{"kind", "hamlet"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"distance", "3.1 km"}}},
                      {FactKind::Place, "keyloc-smelter_ruin-180",
                       {{"kind", "smelter_ruin"}, {"name", "the burnt works"}, {"family", "extractive"}, {"state", "ruined"},
                        {"threat", "2"}, {"occupant", "squatters"}, {"distance", "3.1 km"}}},
                      {FactKind::Place, "hamlet-37", {{"kind", "hamlet"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"distance", "4.0 km"}}},
                      {FactKind::Place, "keyloc-silver_mine-211",
                       {{"kind", "silver_mine"}, {"name", "the deeps under Linbrig"}, {"family", "extractive"}, {"state", "ruined"},
                        {"threat", "5"}, {"occupant", "monsters"}, {"distance", "4.0 km"}}},
                      {FactKind::Place, "fortress-17", {{"kind", "fortress"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"distance", "8.0 km"}}},
                      {FactKind::Place, "surface-city-2-242-dwarf",
                       {{"kind", "city"}, {"name", "Hakndum (Age 2)"}, {"class", "small"}, {"people", "dwarf"}, {"distance", "8.9 km"}}},
                      {FactKind::Place, "surface-city-1-152-human_cold",
                       {{"kind", "city"}, {"name", "Nerstrann (Age 1)"}, {"class", "small"}, {"people", "human_cold"}, {"distance", "14.8 km"}}},
                      {FactKind::Danger, "nest-hippogryphs-179",
                       {{"creature", "Hippogryphs"}, {"danger", "3"}, {"family", "fantastic"}, {"distance", "4.9 km"}}},
                      {FactKind::Danger, "nest-frost-owls-179",
                       {{"creature", "Frost owls"}, {"danger", "2"}, {"family", "fantastic"}, {"distance", "4.9 km"}}},
                  });
}

inline const std::string kOwnOffer = "hook-hero-castellan-fortress-node-178-0";
inline const std::string kBrokeredOffer = "hook-hero-pretender-surface-city-0-243-human_cold-0";

inline FactPacket work() {
    return packet(castellan(), ActKind::AskForWork, Outcome::Answered,
                  {
                      {FactKind::QuestOffer, kOwnOffer,
                       {{"giver", "Selbren"}, {"giver_role", "castellan"}, {"from_speaker", "yes"}, {"verb", "defend"},
                        {"purpose", "Hold the fortress above Wenweg (Age 2) with me."}, {"where", "the fortress of Wenweg (Age 2)"},
                        {"difficulty", "3"}, {"distance", "8.0 km"}, {"days_left", "713"}}},
                      {FactKind::QuestOffer, "hook-hero-harbourmaster-coastal-23-30-0",
                       {{"giver", "Nerbrann"}, {"giver_role", "harbourmaster"}, {"from_speaker", "no"}, {"verb", "trade"},
                        {"purpose", "Keep the sea lanes to the harbour of Wenweg (Age 2) open."}, {"where", "the hamlet of Wenweg (Age 2)"},
                        {"difficulty", "2"}, {"distance", "13.6 km"}, {"days_left", "974"}}},
                      {FactKind::QuestOffer, kBrokeredOffer,
                       {{"giver", "Halbrann"}, {"giver_role", "pretender"}, {"from_speaker", "no"}, {"verb", "reclaim"},
                        {"purpose", "Help me take back Linbrig."}, {"where", "the ruins of Linbrig"}, {"difficulty", "2"},
                        {"distance", "5.2 km"}, {"days_left", "776"}}},
                      {FactKind::QuestOffer, "hook-hero-pretender-surface-city-0-243-human_cold-1",
                       {{"giver", "Halbrann"}, {"giver_role", "pretender"}, {"from_speaker", "no"}, {"verb", "recover"},
                        {"purpose", "Recover the reliquary of Linbrig from the ruins."},
                        {"where", "the reliquary of Linbrig, in the ruins of Linbrig"}, {"difficulty", "2"}, {"distance", "5.2 km"},
                        {"days_left", "581"}}},
                  });
}

inline FactPacket about_self() {
    return packet(castellan(), ActKind::AskAboutPerson, Outcome::Answered,
                  {{FactKind::Person, "hero-castellan-fortress-node-178",
                    {{"name", "Selbren"}, {"role", "castellan"}, {"where", "Wenweg (Age 2)"}, {"status", "about"}}}});
}

inline FactPacket unknown_place() { return packet(castellan(), ActKind::AskAboutPlace, Outcome::Unknown); }

// A broker asked to hand over work that is not theirs: the reason, and who to go to.
inline FactPacket refused_brokered() {
    return packet(castellan(), ActKind::AcceptQuest, Outcome::Refused,
                  {{FactKind::Reason, "", {{"reason", "the work is Halbrann's to give"}, {"why", "not_theirs_to_give"}}},
                   {FactKind::Person, "hero-pretender-surface-city-0-243-human_cold",
                    {{"name", "Halbrann"}, {"role", "pretender"}, {"where", "the ruins of Linbrig"}, {"status", "about"}}}});
}

inline FactPacket accepted(const std::string& quest = kOwnOffer) {
    return packet(castellan(), ActKind::AcceptQuest, Outcome::Accepted, {{FactKind::QuestState, quest, {{"state", "taken"}}}});
}

inline FactPacket declined() { return packet(castellan(), ActKind::DeclineQuest, Outcome::Declined); }

inline FactPacket abandoned(const std::string& quest = kOwnOffer) {
    return packet(castellan(), ActKind::AbandonQuest, Outcome::Abandoned, {{FactKind::QuestState, quest, {{"state", "abandoned"}}}});
}

inline FactPacket parted() { return packet(castellan(), ActKind::Farewell, Outcome::Parted); }

inline FactPacket not_understood() { return packet(gate_captain(), ActKind::NoAct, Outcome::NotUnderstood); }

// The gate captain's view from the fortress: another hamlet of Wenweg is the nearest there.
inline FactPacket surroundings_from_the_gate() {
    return packet(gate_captain(), ActKind::AskSurroundings, Outcome::Answered,
                  {
                      {FactKind::Place, "fortress-17", {{"kind", "fortress"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"distance", "here"}}},
                      {FactKind::Place, "hamlet-38", {{"kind", "hamlet"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"distance", "3.1 km"}}},
                      {FactKind::Place, "keyloc-pass_hospice-146",
                       {{"kind", "pass_hospice"}, {"name", "the hospice on the Wenweg (Age 2) pass"}, {"family", "wayside"},
                        {"state", "ruined"}, {"threat", "4"}, {"occupant", "monsters"}, {"distance", "3.1 km"}}},
                      {FactKind::Place, "hamlet-37", {{"kind", "hamlet"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"distance", "4.9 km"}}},
                      {FactKind::Danger, "nest-glacier-dragons-210",
                       {{"creature", "Glacier dragons"}, {"danger", "5"}, {"family", "draconic"}, {"distance", "3.1 km"}}},
                  });
}

// A roster innkeeper where two givers stand: all of the work they know of is someone else's.
inline Persona innkeeper() {
    Persona p;
    p.uid = "npc-surface-city-1-359-hill_dwarf-inn-0";
    p.name = "Dornwen";
    p.role = "innkeeper";
    p.place = "Torhas (Age 1)";
    return p;
}

inline FactPacket work_at_the_inn() {
    return packet(innkeeper(), ActKind::AskForWork, Outcome::Answered,
                  {
                      {FactKind::QuestOffer, "hook-hero-council-surface-city-1-359-hill_dwarf-commander-0",
                       {{"giver", "Dornar"}, {"giver_role", "council"}, {"from_speaker", "no"}, {"verb", "defend"},
                        {"purpose", "Walk the walls of Torhas (Age 1) with me."}, {"where", "Torhas (Age 1)"}, {"difficulty", "2"},
                        {"distance", "here"}, {"days_left", "948"}}},
                      {FactKind::QuestOffer, "hook-hero-keeper-shrine-ruin-surface-city-0-327-hill_dwarf-0",
                       {{"giver", "Mekmoh"}, {"giver_role", "keeper"}, {"from_speaker", "no"}, {"verb", "tend"},
                        {"purpose", "Bring an offering to the shrine of The Red Field at Narbod."}, {"difficulty", "1"},
                        {"distance", "2.2 km"}, {"days_left", "313"}}},
                  });
}

inline FactPacket surroundings_from_the_inn() {
    return packet(innkeeper(), ActKind::AskSurroundings, Outcome::Answered,
                  {
                      {FactKind::Place, "surface-city-1-359-hill_dwarf",
                       {{"kind", "city"}, {"name", "Torhas (Age 1)"}, {"class", "capital"}, {"people", "hill_dwarf"}, {"distance", "here"}}},
                      {FactKind::Place, "keyloc-boundary_stone-march_stones-frontier-98600",
                       {{"kind", "boundary_stone"}, {"name", "the cold marker"}, {"family", "wayside"}, {"state", "abandoned"},
                        {"threat", "3"}, {"distance", "here"}}},
                      {FactKind::Place, "keyloc-border_fort-391",
                       {{"kind", "border_fort"}, {"name", "the silent keep"}, {"family", "fortification"}, {"state", "ruined"},
                        {"threat", "4"}, {"occupant", "squatters"}, {"distance", "3.1 km"}}},
                      {FactKind::Place, "surface-city-2-454-human_desert",
                       {{"kind", "city"}, {"name", "Dunfor (Age 2)"}, {"class", "small"}, {"people", "human_desert"}, {"distance", "11.2 km"}}},
                      {FactKind::Place, "surface-city-0-425-human_desert",
                       {{"kind", "city"}, {"name", "Strandbrig"}, {"class", "small"}, {"people", "human_desert"}, {"distance", "13.5 km"}}},
                  });
}

// Every packet above, for tests that hold across all of them.
inline std::vector<FactPacket> all() {
    return {surroundings(), work(), about_self(), unknown_place(), refused_brokered(), accepted(), declined(), abandoned(), parted(),
            not_understood(), surroundings_from_the_gate(), work_at_the_inn(), surroundings_from_the_inn()};
}

}  // namespace packets
