// Fact packets as the director writes them, built by hand: this component never sees a world.
// Their text is taken from real packets (seed 42, size 33, a month old), so the tests read what
// the voice and the interpreter will actually be handed. The ids are the director's too, but for
// the work's markers (for the game's map, never said), whose ids are illustrative.
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

// What the speaker remembers of the stranger, as the director's Memory fact says it
// (orchestrator/contract.hpp): by default a first meeting, on its first turn.
struct Recall {
    std::string met = "first";
    int times = 0;
    std::string since;
    std::string mood = "neutral";
    std::string work;
    std::string asked_before;
    int turn = 1;
    bool repeat = false;
    int heard = 0;
    bool exposed = false;
};

inline Fact memory_of(const std::string& speaker, const Recall& r) {
    return {FactKind::Memory, speaker,
            {{"met", r.met},
             {"times", std::to_string(r.times)},
             {"since", r.since},
             {"mood", r.mood},
             {"work", r.work},
             {"asked_before", r.asked_before},
             {"turn", std::to_string(r.turn)},
             {"repeat", r.repeat ? "yes" : "no"},
             {"heard", std::to_string(r.heard)},
             {"exposed", r.exposed ? "yes" : "no"}}};
}

// A packet as the director writes one: its facts, then the speaker's memory.
inline FactPacket packet(Persona persona, ActKind answering, Outcome outcome, std::vector<Fact> facts = {}, const Recall& recall = {}) {
    FactPacket p;
    p.speaker = persona.uid;
    p.answering = answering;
    p.outcome = outcome;
    p.persona = std::move(persona);
    p.facts = std::move(facts);
    p.facts.push_back(memory_of(p.speaker, recall));
    return p;
}

inline FactPacket surroundings() {
    return packet(castellan(), ActKind::AskSurroundings, Outcome::Answered,
                  {
                      {FactKind::Place, "surface-city-2-212-human_cold",
                       {{"kind", "city"}, {"name", "Wenweg (Age 2)"}, {"class", "small"}, {"people", "human_cold"}, {"range", "here"}}},
                      {FactKind::Place, "keyloc-waystone-road_waystones-rsurface-city-2-142-human_cold-surface-city-2-212-human_cold-19800",
                       {{"kind", "waystone"}, {"name", "the ninth stone on the Linbrig road"}, {"family", "wayside"}, {"state", "abandoned"},
                        {"threat", "4"}, {"occupant", "monsters"}, {"range", "near"}}},
                      {FactKind::Place, "hamlet-39", {{"kind", "hamlet"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"range", "walk"}}},
                      {FactKind::Place, "keyloc-smelter_ruin-180",
                       {{"kind", "smelter_ruin"}, {"name", "the burnt works"}, {"family", "extractive"}, {"state", "ruined"},
                        {"threat", "2"}, {"occupant", "squatters"}, {"range", "walk"}}},
                      {FactKind::Place, "hamlet-37", {{"kind", "hamlet"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"range", "walk"}}},
                      {FactKind::Place, "keyloc-silver_mine-211",
                       {{"kind", "silver_mine"}, {"name", "the deeps under Linbrig"}, {"family", "extractive"}, {"state", "ruined"},
                        {"threat", "5"}, {"occupant", "monsters"}, {"range", "walk"}}},
                      {FactKind::Place, "fortress-17", {{"kind", "fortress"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"range", "far"}}},
                      {FactKind::Place, "surface-city-2-242-dwarf",
                       {{"kind", "city"}, {"name", "Hakndum (Age 2)"}, {"class", "small"}, {"people", "dwarf"}, {"range", "far"}}},
                      {FactKind::Place, "surface-city-1-152-human_cold",
                       {{"kind", "city"}, {"name", "Nerstrann (Age 1)"}, {"class", "small"}, {"people", "human_cold"}, {"range", "far"}}},
                      {FactKind::Danger, "nest-hippogryphs-179",
                       {{"creature", "Hippogryphs"}, {"danger", "3"}, {"family", "fantastic"}, {"range", "walk"}}},
                      {FactKind::Danger, "nest-frost-owls-179",
                       {{"creature", "Frost owls"}, {"danger", "2"}, {"family", "fantastic"}, {"range", "walk"}}},
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
                        {"difficulty", "3"}, {"range", "far"}, {"days_left", "713"}, {"marker", "fortress-17"},
                        {"marker_node", "178"}}},
                      {FactKind::QuestOffer, "hook-hero-harbourmaster-coastal-23-30-0",
                       {{"giver", "Nerbrann"}, {"giver_role", "harbourmaster"}, {"from_speaker", "no"}, {"verb", "trade"},
                        {"purpose", "Keep the sea lanes to the harbour of Wenweg (Age 2) open."}, {"where", "the hamlet of Wenweg (Age 2)"},
                        {"difficulty", "2"}, {"range", "far"}, {"days_left", "974"}, {"marker", "hamlet-38"}, {"marker_node", "30"}}},
                      {FactKind::QuestOffer, kBrokeredOffer,
                       {{"giver", "Halbrann"}, {"giver_role", "pretender"}, {"from_speaker", "no"}, {"verb", "reclaim"},
                        {"purpose", "Help me take back Linbrig."}, {"where", "the ruins of Linbrig"}, {"difficulty", "2"},
                        {"range", "walk"}, {"days_left", "776"}, {"marker", "ruin-243"}, {"marker_node", "243"}}},
                      {FactKind::QuestOffer, "hook-hero-pretender-surface-city-0-243-human_cold-1",
                       {{"giver", "Halbrann"}, {"giver_role", "pretender"}, {"from_speaker", "no"}, {"verb", "recover"},
                        {"purpose", "Recover the reliquary of Linbrig from the ruins."},
                        {"where", "the reliquary of Linbrig, in the ruins of Linbrig"}, {"difficulty", "2"}, {"range", "walk"},
                        {"days_left", "581"}, {"marker", "ruin-243"}, {"marker_node", "243"}}},
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
    return packet(castellan(), ActKind::AcceptQuest, Outcome::Accepted,
                  {{FactKind::QuestState, quest, {{"state", "taken"}, {"marker", "fortress-17"}, {"marker_node", "178"}}}});
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
                      {FactKind::Place, "fortress-17", {{"kind", "fortress"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"range", "here"}}},
                      {FactKind::Place, "hamlet-38", {{"kind", "hamlet"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"range", "walk"}}},
                      {FactKind::Place, "keyloc-pass_hospice-146",
                       {{"kind", "pass_hospice"}, {"name", "the hospice on the Wenweg (Age 2) pass"}, {"family", "wayside"},
                        {"state", "ruined"}, {"threat", "4"}, {"occupant", "monsters"}, {"range", "walk"}}},
                      {FactKind::Place, "hamlet-37", {{"kind", "hamlet"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"range", "walk"}}},
                      {FactKind::Danger, "nest-glacier-dragons-210",
                       {{"creature", "Glacier dragons"}, {"danger", "5"}, {"family", "draconic"}, {"range", "walk"}}},
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
                        {"range", "here"}, {"days_left", "948"}}},
                      {FactKind::QuestOffer, "hook-hero-keeper-shrine-ruin-surface-city-0-327-hill_dwarf-0",
                       {{"giver", "Mekmoh"}, {"giver_role", "keeper"}, {"from_speaker", "no"}, {"verb", "tend"},
                        {"purpose", "Bring an offering to the shrine of The Red Field at Narbod."}, {"difficulty", "1"},
                        {"range", "near"}, {"days_left", "313"}}},
                  });
}

inline FactPacket surroundings_from_the_inn() {
    return packet(innkeeper(), ActKind::AskSurroundings, Outcome::Answered,
                  {
                      {FactKind::Place, "surface-city-1-359-hill_dwarf",
                       {{"kind", "city"}, {"name", "Torhas (Age 1)"}, {"class", "capital"}, {"people", "hill_dwarf"}, {"range", "here"}}},
                      {FactKind::Place, "keyloc-boundary_stone-march_stones-frontier-98600",
                       {{"kind", "boundary_stone"}, {"name", "the cold marker"}, {"family", "wayside"}, {"state", "abandoned"},
                        {"threat", "3"}, {"range", "here"}}},
                      {FactKind::Place, "keyloc-border_fort-391",
                       {{"kind", "border_fort"}, {"name", "the silent keep"}, {"family", "fortification"}, {"state", "ruined"},
                        {"threat", "4"}, {"occupant", "squatters"}, {"range", "walk"}}},
                      {FactKind::Place, "surface-city-2-454-human_desert",
                       {{"kind", "city"}, {"name", "Dunfor (Age 2)"}, {"class", "small"}, {"people", "human_desert"}, {"range", "far"}}},
                      {FactKind::Place, "surface-city-0-425-human_desert",
                       {{"kind", "city"}, {"name", "Strandbrig"}, {"class", "small"}, {"people", "human_desert"}, {"range", "far"}}},
                  });
}

// Every packet above, for tests that hold across all of them.
inline std::vector<FactPacket> all() {
    return {surroundings(), work(), about_self(), unknown_place(), refused_brokered(), accepted(), declined(), abandoned(), parted(),
            not_understood(), surroundings_from_the_gate(), work_at_the_inn(), surroundings_from_the_inn()};
}

// ---- Contract version 4: conversation --------------------------------------------------------

// Greetings, by what the castellan remembers of the stranger.
inline FactPacket greeted_first() { return packet(castellan(), ActKind::Greet, Outcome::Greeted); }

inline FactPacket greeted_again() {
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {},
                  {.met = "again", .times = 1, .since = "days", .asked_before = "the burnt works"});
}

inline FactPacket greeted_warmly() {
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {}, {.met = "again", .times = 3, .since = "weeks", .mood = "warm"});
}

inline FactPacket greeted_coldly() {
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {}, {.met = "again", .times = 2, .since = "today", .mood = "hostile"});
}

// What came of the castellan's own work, remembered at the next meeting.
inline FactPacket greeted_after(const std::string& work, const std::string& mood) {
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {},
                  {.met = "again", .times = 1, .since = "days", .mood = mood, .work = work});
}

// Small talk where the castellan stands, in weather of each kind. The season and the hour as the
// director words them: one of each that no article before its blank fits both ways ("a evening",
// "this the turn of the season").
inline FactPacket small_talk(const std::string& weather, const std::string& wind, const std::string& mood = "neutral",
                             const std::string& season = "deep winter", const std::string& time = "evening") {
    return packet(castellan(), ActKind::SmallTalk, Outcome::Chatted,
                  {{FactKind::Chat, castellan().uid, {{"weather", weather}, {"wind", wind}, {"season", season}, {"time", time}}},
                   {FactKind::Danger, "nest-hippogryphs-179",
                    {{"creature", "Hippogryphs"}, {"danger", "3"}, {"family", "fantastic"}, {"range", "walk"}}}},
                  {.mood = mood});
}

// A farewell from a speaker the stranger has put out: the castellan, wary after an insult.
inline FactPacket parted_coldly() {
    return packet(castellan(), ActKind::Farewell, Outcome::Parted, {}, {.met = "again", .times = 1, .since = "yesterday", .mood = "wary", .turn = 3});
}

inline FactPacket acknowledged() { return packet(castellan(), ActKind::Answer, Outcome::Acknowledged, {}, {.turn = 2}); }

inline FactPacket thanked(const std::string& mood = "friendly") {
    return packet(castellan(), ActKind::Thank, Outcome::Thanked, {}, {.mood = mood, .turn = 3});
}

inline FactPacket offended(bool again) {
    return packet(castellan(), ActKind::Insult, Outcome::Offended, {},
                  {.mood = again ? "hostile" : "wary", .turn = again ? 3 : 2, .repeat = again});
}

// "Is it safe", from the gate and from the inn: the verdict, the places it rests on first, the nests.
inline FactPacket safety_unsafe() {
    return packet(gate_captain(), ActKind::AskSafety, Outcome::Answered,
                  {{FactKind::Safety, "", {{"verdict", "unsafe"}}},
                   {FactKind::Place, "keyloc-pass_hospice-146",
                    {{"kind", "pass_hospice"}, {"name", "the hospice on the Wenweg (Age 2) pass"}, {"family", "wayside"},
                     {"state", "ruined"}, {"threat", "4"}, {"occupant", "monsters"}, {"range", "walk"}}},
                   {FactKind::Danger, "nest-glacier-dragons-210",
                    {{"creature", "Glacier dragons"}, {"danger", "5"}, {"family", "draconic"}, {"range", "walk"}}}});
}

inline FactPacket safety_wary() {
    return packet(innkeeper(), ActKind::AskSafety, Outcome::Answered,
                  {{FactKind::Safety, "", {{"verdict", "wary"}}},
                   {FactKind::Place, "keyloc-border_fort-391",
                    {{"kind", "border_fort"}, {"name", "the silent keep"}, {"family", "fortification"}, {"state", "ruined"},
                     {"threat", "4"}, {"occupant", "squatters"}, {"range", "walk"}}}});
}

inline FactPacket safety_safe() {
    return packet(castellan(), ActKind::AskSafety, Outcome::Answered,
                  {{FactKind::Safety, "", {{"verdict", "safe"}}},
                   {FactKind::Place, "keyloc-silver_mine-211",
                    {{"kind", "silver_mine"}, {"name", "the deeps under Linbrig"}, {"family", "extractive"}, {"state", "ruined"},
                     {"threat", "5"}, {"occupant", "monsters"}, {"range", "far"}}}});
}

// Asked what is around a second time in one conversation.
inline FactPacket surroundings_again() {
    FactPacket p = surroundings();
    p.facts.back() = memory_of(p.speaker, {.turn = 2, .repeat = true});
    return p;
}

// A deceiver's true face, owned to: a magister under a midwife's mask (seed 99's kind).
inline Persona magister_unmasked() {
    Persona p;
    p.uid = "hero-magister-surface-city-1-123-human_cold";
    p.name = "Dunbrann";
    p.role = "magister";
    p.place = "Nerstrann (Age 1)";
    p.toward_player = "Offers a fair trade and expects the same. Proves the player's standing before trusting them with anything "
                      "that matters.";
    p.never = {"reveal the true face voluntarily", "break a sworn word", "act outside the office they hold", "cruelty for its own sake"};
    p.voice_key = "deceiver/LN";
    p.face = "true";
    return p;
}

inline FactPacket exposed_mask() {
    return packet(magister_unmasked(), ActKind::Confront, Outcome::Exposed,
                  {{FactKind::Reveal, magister_unmasked().uid, {{"face", "true"}, {"was", "midwife"}, {"is", "magister"}}}},
                  {.met = "again", .times = 2, .since = "days", .turn = 2, .exposed = true});
}

// A beast's calm face, owning to what the stranger saw under the full moon.
inline Persona beast_calm() {
    Persona p;
    p.uid = "hero-beast-hamlet-node-205";
    p.name = "Harnwen";
    p.role = "trapper";
    p.place = "the hamlet of Wenweg (Age 2)";
    p.voice_key = "beast/NG";
    return p;
}

inline FactPacket exposed_beast() {
    return packet(beast_calm(), ActKind::Confront, Outcome::Exposed,
                  // A beast's two faces share one role: only a mask's differ.
                  {{FactKind::Reveal, beast_calm().uid, {{"face", "unleashed"}, {"was", "trapper"}, {"is", "trapper"}}}},
                  {.met = "again", .times = 3, .since = "days", .exposed = true});
}

// A fractured heart's first face, owning to the second the stranger met on another day.
inline Persona heart_first() {
    Persona p;
    p.uid = "hero-oathkeeper-surface-city-0-205-frosthold_dwarf";
    p.name = "Brandis";
    p.role = "oathkeeper";
    p.place = "Hakndum (Age 2)";
    p.voice_key = "fractured_heart/LN";
    return p;
}

inline FactPacket exposed_heart() {
    return packet(heart_first(), ActKind::Confront, Outcome::Exposed,
                  {{FactKind::Reveal, heart_first().uid, {{"face", "second"}, {"was", "oathkeeper"}, {"is", "oathkeeper"}}}},
                  {.met = "again", .times = 1, .since = "days", .exposed = true});
}

inline FactPacket denied(bool again) {
    return packet(castellan(), ActKind::Confront, Outcome::Denied, {}, {.turn = again ? 3 : 2, .repeat = again});
}

// Every conversation packet, for the tests and the written lines' rules.
inline std::vector<FactPacket> conversation() {
    return {greeted_first(),
            greeted_again(),
            greeted_warmly(),
            greeted_coldly(),
            greeted_after("taken", "friendly"),
            greeted_after("finished", "warm"),
            greeted_after("failed", "neutral"),
            greeted_after("abandoned", "neutral"),
            small_talk("clear", "breezy"),
            small_talk("unsettled", "windy"),
            small_talk("stormy", "a gale"),
            small_talk("fair", "still", "hostile"),
            small_talk("fair", "breezy", "friendly", "the turn of the season", "afternoon"),
            parted_coldly(),
            acknowledged(),
            thanked(),
            thanked("hostile"),
            offended(false),
            offended(true),
            safety_unsafe(),
            safety_wary(),
            safety_safe(),
            surroundings_again(),
            exposed_mask(),
            exposed_beast(),
            exposed_heart(),
            denied(false),
            denied(true)};
}

}  // namespace packets
