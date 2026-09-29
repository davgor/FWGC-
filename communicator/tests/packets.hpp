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
    std::string feeling = "calm";
    std::string resting = "calm";
    bool eased = false;
    bool joined = false;
    bool beside = false;
    bool work_news = false;  // contract version 17
    std::string called, called_how, called_other;  // contract version 24
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
             {"exposed", r.exposed ? "yes" : "no"},
             {"feeling", r.feeling},
             {"resting", r.resting},
             {"eased", r.eased ? "yes" : "no"},
             {"joined", r.joined ? "yes" : "no"},
             {"beside", r.beside ? "yes" : "no"},
             {"work_news", r.work_news ? "yes" : "no"},
             {"called", r.called},
             {"called_how", r.called_how},
             {"called_other", r.called_other}}};
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

// A farewell to someone who holds the speaker's work, still to do.
inline FactPacket parted_holding_work() {
    return packet(castellan(), ActKind::Farewell, Outcome::Parted, {}, {.met = "again", .times = 1, .since = "days", .mood = "friendly", .work = "taken"});
}

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
    // Finished or failed, as the first conversation after it has it: news between them (contract
    // version 17).
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {},
                  {.met = "again", .times = 1, .since = "days", .mood = mood, .work = work,
                   .work_news = work == "finished" || work == "failed"});
}

// The same weeks on, in a later conversation: what came of it is no longer news.
inline FactPacket greeted_long_after(const std::string& work, const std::string& mood) {
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {},
                  {.met = "again", .times = 2, .since = "weeks", .mood = mood, .work = work});
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

// What the stranger tells, to a speaker whose own work stands at `work` (contract version 13).
inline FactPacket acknowledged_with(const std::string& work, const std::string& mood = "neutral") {
    return packet(castellan(), ActKind::Answer, Outcome::Acknowledged, {}, {.mood = mood, .work = work, .turn = 2});
}

// Thanks, or an insult, to a speaker whose own work stands at `work` (contract version 13).
inline FactPacket thanked_with_work(const std::string& work) {
    return packet(castellan(), ActKind::Thank, Outcome::Thanked, {}, {.mood = "friendly", .work = work, .turn = 3});
}

inline FactPacket offended_after(const std::string& work) {
    return packet(castellan(), ActKind::Insult, Outcome::Offended, {}, {.mood = "wary", .work = work, .turn = 2});
}

// A complaint about the castellan's work, heard out (contract version 18).
inline FactPacket heard(const std::string& work = "finished") {
    return packet(castellan(), ActKind::Complain, Outcome::Heard, {},
                  {.met = "again", .times = 1, .since = "days", .mood = "friendly", .work = work, .turn = 2});
}

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

// An apology, to the castellan: a grievance eased, with the mood it leaves; or nothing to forgive.
inline FactPacket forgiven(const std::string& mood) {
    return packet(castellan(), ActKind::Apologize, Outcome::Forgiven, {}, {.met = "again", .times = 1, .since = "today", .mood = mood, .turn = 2});
}

inline FactPacket unwronged() { return packet(castellan(), ActKind::Apologize, Outcome::Unwronged, {}, {.turn = 2}); }

// Asked about the town they stand in (contract version 8): its trades, a hero there, a nest
// around it, and work there.
inline FactPacket about_the_town() {
    return packet(castellan(), ActKind::AskAboutPlace, Outcome::Answered,
                  {{FactKind::Place, "surface-city-2-212-human_cold",
                    {{"kind", "city"}, {"name", "Wenweg (Age 2)"}, {"class", "small"}, {"people", "human_cold"}, {"range", "here"},
                     {"trades", "administrator, smith, officiant"}}},
                   {FactKind::Person, "hero-council-surface-city-2-212-human_cold-steward",
                    {{"name", "Tornar"}, {"role", "council"}, {"where", "Wenweg (Age 2)"}, {"status", "about"}}},
                   {FactKind::Danger, "nest-frost-owls-212",
                    {{"creature", "Frost owls"}, {"danger", "2"}, {"family", "fantastic"}, {"range", "close"}, {"around", "yes"}}},
                   {FactKind::QuestOffer, "hook-hero-council-surface-city-2-212-human_cold-steward-0",
                    {{"giver", "Tornar"}, {"giver_role", "council"}, {"from_speaker", "no"}, {"verb", "defend"},
                     {"purpose", "Walk the walls of Wenweg (Age 2) with me."}, {"where", "Wenweg (Age 2)"}, {"difficulty", "2"},
                     {"range", "here"}, {"days_left", "700"}, {"marker", "surface-city-2-212-human_cold"}, {"marker_node", "212"}}}});
}

// Asked about the fortress the castellan keeps: one trade, two heroes there, nests right by it and
// a short walk from it, and the speaker's own work there.
inline FactPacket about_the_fortress() {
    return packet(castellan(), ActKind::AskAboutPlace, Outcome::Answered,
                  {{FactKind::Place, "fortress-17",
                    {{"kind", "fortress"}, {"of", "Wenweg (Age 2)"}, {"people", "human_cold"}, {"range", "far"}, {"trades", "gate_guard"}}},
                   {FactKind::Person, "hero-warden-fortress-node-178",
                    {{"name", "Froswen"}, {"role", "warden"}, {"where", "the fortress of Wenweg (Age 2)"}, {"status", "about"}}},
                   {FactKind::Person, "hero-council-fortress-node-178",
                    {{"name", "Legmok"}, {"role", "council"}, {"where", "the fortress of Wenweg (Age 2)"}, {"status", "about"}}},
                   {FactKind::Danger, "nest-glacier-dragons-178",
                    {{"creature", "Glacier dragons"}, {"danger", "5"}, {"family", "draconic"}, {"range", "here"}, {"around", "yes"}}},
                   {FactKind::Danger, "nest-basilisks-160",
                    {{"creature", "Basilisks"}, {"danger", "4"}, {"family", "fantastic"}, {"range", "near"}, {"around", "yes"}}},
                   {FactKind::QuestOffer, kOwnOffer,
                    {{"giver", "Selbren"}, {"giver_role", "castellan"}, {"from_speaker", "yes"}, {"verb", "defend"},
                     {"purpose", "Hold the fortress above Wenweg (Age 2) with me."}, {"where", "the fortress of Wenweg (Age 2)"},
                     {"difficulty", "3"}, {"range", "far"}, {"days_left", "713"}, {"marker", "fortress-17"}, {"marker_node", "178"}}}});
}

// ---- Contract version 9: feelings ---------------------------------------------------------------

// Calming words that eased the castellan a step, to `feeling`.
inline FactPacket calmed(const std::string& feeling) {
    return packet(castellan(), ActKind::Calm, Outcome::Calmed, {}, {.mood = "friendly", .turn = 2, .feeling = feeling, .eased = true});
}

// Calming words that did not: what frightened them is still there, or they distrust the stranger.
inline FactPacket still_afraid(const std::string& mood = "neutral") {
    return packet(castellan(), ActKind::Calm, Outcome::StillAfraid, {}, {.mood = mood, .turn = 2, .feeling = "panicked"});
}

// Calming words to someone at rest: calm, or uneasy by a dangerous nest.
inline FactPacket not_afraid(const std::string& resting = "calm") {
    return packet(castellan(), ActKind::Calm, Outcome::NotAfraid, {}, {.turn = 2, .feeling = resting, .resting = resting});
}

// A frightened castellan greeted, chatting, thanked, parting, and asked what is around.
inline FactPacket frightened(ActKind act, Outcome outcome, const std::string& feeling) {
    FactPacket p = act == ActKind::AskSurroundings ? surroundings() : packet(castellan(), act, outcome);
    if (act == ActKind::SmallTalk) p = small_talk("unsettled", "windy");
    p.facts.back() = memory_of(p.speaker, {.turn = 2, .feeling = feeling});
    return p;
}

// ---- Contract version 10: remembered easing, and a bystander who joins -----------------------

// Calming words to someone at rest whom the player talked down lately.
inline FactPacket not_afraid_eased() {
    return packet(castellan(), ActKind::Calm, Outcome::NotAfraid, {}, {.turn = 2, .eased = true});
}

// Met again after the player talked them down last time.
inline FactPacket greeted_eased() {
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {}, {.met = "again", .times = 1, .since = "today", .eased = true});
}

// What is around, from a bystander who overheard it asked of someone else and joined in.
inline FactPacket joined_surroundings() {
    FactPacket p = surroundings();
    p.facts.back() = memory_of(p.speaker, {.joined = true});
    return p;
}

inline FactPacket denied(bool again) {
    return packet(castellan(), ActKind::Confront, Outcome::Denied, {}, {.turn = again ? 3 : 2, .repeat = again});
}

// ---- Contract version 11: news ---------------------------------------------------------------

// News of a piece of work, as the director writes it: Halbrann's call to defend Wenweg, at `state`,
// the stranger's doing or not, heard by word.
inline Fact news_of_work(const std::string& state, bool players) {
    return {FactKind::News,
            "event-12",
            {{"event", "work"},
             {"state", state},
             {"place", "Wenweg"},
             {"verb", "defend"},
             {"purpose", "Stand with Wenweg when they come for it."},
             {"giver", "Halbrann"},
             {"player", players ? "yes" : "no"},
             {"since", "yesterday"},  // fresh: talk of the town, brought up by every speaker
             {"how", "word"}}};
}

// News of a scare at the fortress, ended as `state`, by `by` ("player", a name, or nobody).
inline Fact news_of_a_scare(const std::string& state, const std::string& by) {
    Fact f{FactKind::News, "event-31", {{"event", "scare"}, {"state", state}, {"place", "the fortress of Wenweg"}}};
    f.fields.emplace_back("player", by == "player" ? "yes" : "no");
    if (!by.empty()) f.fields.emplace_back("by", by);
    f.fields.emplace_back("since", "today");
    f.fields.emplace_back("how", "word");
    return f;
}

// A greeting from someone who heard what the stranger did.
inline FactPacket greeted_having_heard() {
    return packet(gate_captain(), ActKind::Greet, Outcome::Greeted, {news_of_work("finished", true)});
}

// Small talk from someone who heard the stranger beat off a threat where they stand.
inline FactPacket small_talk_with_news() {
    FactPacket p = small_talk("clear", "breezy");
    p.facts.insert(p.facts.end() - 1, news_of_a_scare("beaten", "player"));  // the Memory fact stays last
    return p;
}

// A greeting from someone who heard what came of Halbrann's work at the stranger's hands, at
// `state`: taken, failed or given up as well as finished. The rows were never tried with a failure,
// which the truth check refused (writers found it, 2026-09-27).
inline FactPacket greeted_having_heard_of(const std::string& state) {
    return packet(gate_captain(), ActKind::Greet, Outcome::Greeted, {news_of_work(state, true)});
}

// Small talk from someone who heard word of Halbrann's work at `state`, the stranger's doing or not.
inline FactPacket small_talk_with_word_of(const std::string& state, bool players) {
    FactPacket p = small_talk("clear", "breezy");
    p.facts.insert(p.facts.end() - 1, news_of_work(state, players));  // the Memory fact stays last
    return p;
}

// An answer about the town, with news of it: work asked for there, which is nobody's doing yet, and
// a threat Orlan beat off.
inline FactPacket about_the_town_with_news() {
    FactPacket p = about_the_town();
    p.facts.insert(p.facts.end() - 1, news_of_work("asked", false));
    p.facts.insert(p.facts.end() - 1, news_of_a_scare("beaten", "Orlan"));
    return p;
}

// ---- Contract version 26: heroes' deeds ---------------------------------------------------------

// News of what Selbren did (`verb` defended, hunted, assaulted; `state` won or lost) against the glacier
// dragons, today: heard by word, or theirs, being there.
inline Fact news_of_a_heros_deed(const std::string& verb, const std::string& state, const std::string& how) {
    return {FactKind::News,
            "event-40",
            {{"event", "hero"},
             {"state", state},
             {"place", verb == "assaulted" ? "the ruins of Linbrig" : "the fortress of Wenweg"},
             {"person", "Selbren"},
             {"verb", verb},
             {"against", "the glacier dragons"},
             {"player", "no"},
             {"since", "today"},
             {"how", how}}};
}

// A greeting from Selbren, who did it, to a stranger met a few days before: their own deed, their side of it.
inline FactPacket greeted_after_own_deed(const std::string& state) {
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {news_of_a_heros_deed("defended", state, "there")},
                  {.met = "again", .times = 1, .since = "days"});
}

// Small talk from the gate captain, who heard of Selbren's deed; `tie` how Selbren stands to them (contract
// version 28): "sovereign", "ally", "kin", "rival", or nothing.
inline FactPacket small_talk_with_a_heros_deed(const std::string& tie = "", const std::string& state = "won") {
    FactPacket p = packet(gate_captain(), ActKind::SmallTalk, Outcome::Chatted,
                          {{FactKind::Chat, gate_captain().uid, {{"weather", "clear"}, {"wind", "still"}, {"season", "summer"}, {"time", "morning"}}}});
    Fact deed = news_of_a_heros_deed("defended", state, "word");
    if (!tie.empty()) deed.fields.emplace_back("tie", tie);
    p.facts.insert(p.facts.end() - 1, std::move(deed));  // the Memory fact stays last
    return p;
}

// ---- Contract version 12: what the speaker recalls of the stranger ------------------------------

// A Recall fact, as the director writes one.
inline Fact recall_fact(const std::string& subject, const std::string& memory, const std::string& act, const std::string& about,
                        int times, const std::string& since, const std::string& first, const std::string& words = "") {
    return {FactKind::Recall,
            subject,
            {{"memory", memory},
             {"act", act},
             {"about", about},
             {"times", std::to_string(times)},
             {"since", since},
             {"first", first},
             {"words", words}}};
}

// Met again, remembering what the stranger once told them of themselves: `words`, as they said them.
inline FactPacket greeted_remembering(const std::string& words = "My sister keeps the mill at the ford.") {
    const std::string self = castellan().uid;
    return packet(castellan(), ActKind::Greet, Outcome::Greeted,
                  {recall_fact(self, "last", "answer", "", 1, "days", "days"), recall_fact(self, "said", "said", "", 1, "days", "days", words)},
                  {.met = "again", .times = 1, .since = "days"});
}

// An apology with the stranger's latest words of this conversation (contract version 23): nothing to
// forgive, or, with `outcome` Forgiven, a wrong forgiven.
inline FactPacket apology_after(const std::string& words, Outcome outcome = Outcome::Unwronged) {
    const std::string self = castellan().uid;
    return packet(castellan(), ActKind::Apologize, outcome, {recall_fact(self, "said", "said", "", 1, "today", "today", words)},
                  {.met = "again", .times = 1, .since = "days"});
}

// Greeted in a later conversation, with a question of the castellan's the stranger never answered
// (contract version 21).
inline FactPacket greeted_with_a_question(const std::string& question = "Who were they to you?") {
    const std::string self = castellan().uid;
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {recall_fact(self, "question", "asked", "", 1, "days", "days", question)},
                  {.met = "again", .times = 1, .since = "days"});
}

// Asked about the town again, having asked of it and been told of it `since` before.
inline FactPacket about_the_town_told(const std::string& since = "weeks") {
    FactPacket p = about_the_town();
    const std::string town = p.facts.front().subject;
    p.facts.insert(p.facts.end() - 1, recall_fact(town, "asked", "ask_about_place", "Wenweg", 1, since, since));
    p.facts.insert(p.facts.end() - 1, recall_fact(town, "told", "place", "Wenweg", 1, since, since));
    p.facts.back() = memory_of(p.speaker, {.met = "again", .times = 1, .since = since});
    return p;
}

// Every conversation packet, for the tests and the written lines' rules.
// Contract version 13: the castellan asked about himself, with a story to tell: what he goes by, how
// he stands, what he is after, a city lost, his deeds, an ally and a rival.
inline FactPacket about_them() {
    return packet(castellan(), ActKind::AskAboutThem, Outcome::Answered,
                  {{FactKind::Self, "hero-castellan-fortress-node-178",
                    {{"role", "castellan"},
                     {"at", "Wenweg (Age 2)"},
                     {"known_as", "the Shield of Wenweg"},
                     {"fame", "notable"},
                     {"home", "Wenweg (Age 2)"},
                     {"stands", "garrison"},
                     {"after", "hold"},
                     {"after_what", "the fortress above Wenweg (Age 2)"},
                     {"lost", "Brenlin"},
                     {"lost_to", "dragon"},
                     {"lost_when", "the last age"},
                     {"since", "the last age"}}},
                   {FactKind::Deed, "fortress-node-178",
                    {{"deed", "fortress"}, {"part", "keeper"}, {"place", "the fortress of Wenweg (Age 2)"}, {"when", "this age"}}},
                   {FactKind::Deed, "ruin-surface-city-0-153-human_cold",
                    {{"deed", "ruin"}, {"part", "credited"}, {"place", "Brenlin"}, {"when", "the last age"}}},
                   {FactKind::Person, "hero-council-surface-city-2-212-human_cold-steward",
                    {{"name", "Tornar"}, {"role", "council"}, {"where", "Wenweg (Age 2)"}, {"status", "about"}, {"tie", "ally"}}},
                   {FactKind::Person, "hero-warlord-1-surface-city-0-205-frosthold_dwarf",
                    {{"name", "Ulknord"}, {"role", "warlord"}, {"status", "about"}, {"tie", "rival"}}},
                   {FactKind::Danger, "nest-frost-owls-212",
                    {{"creature", "Frost owls"}, {"danger", "2"}, {"family", "fantastic"}, {"range", "close"}}}});
}

// Asked about themself after a fight of their own since the stranger was last here (contract version 26): the
// deed rides, theirs, a few days back.
inline FactPacket about_them_after_a_fight(const std::string& state = "lost") {
    FactPacket p = about_them();
    Fact deed = news_of_a_heros_deed("defended", state, "there");
    for (auto& [key, value] : deed.fields)
        if (key == "since") value = "days";
    p.facts.insert(p.facts.end() - 1, std::move(deed));
    return p;
}

// Asked about themself, with no rival on record: an ally, and nobody against them.
inline FactPacket about_them_without_rivals() {
    FactPacket p = about_them();
    std::erase_if(p.facts, [](const Fact& f) { return f.kind == FactKind::Person && f.field("tie") && *f.field("tie") == "rival"; });
    return p;
}

// Asked about themself, with no past on record: no city lost, and no deed but the post they keep, which
// their standing says (every benchmark hero's record, 2026-09-28).
inline FactPacket about_them_without_a_past() {
    FactPacket p = about_them();
    std::erase_if(p.facts, [](const Fact& f) { return f.kind == FactKind::Deed && f.field("part") && *f.field("part") != "keeper"; });
    for (Fact& f : p.facts)
        if (f.kind == FactKind::Self)
            std::erase_if(f.fields, [](const auto& field) { return field.first.rfind("lost", 0) == 0; });
    return p;
}

// Asked about themself, a castellan with a past (contract version 19): twelve years at the post, a sergeant
// of the garrison before it, sworn to the realm's sovereign, and a war their city won in their lifetime.
inline FactPacket about_a_castellan_with_a_past() {
    FactPacket p = about_them_without_a_past();
    for (Fact& f : p.facts)
        if (f.kind == FactKind::Self) {
            f.fields.emplace_back("held_years", "12");
            f.fields.emplace_back("served_at", "the garrison of Wenweg (Age 2)");
            f.fields.emplace_back("served_as", "sergeant");
        }
    const auto first_person = std::find_if(p.facts.begin(), p.facts.end(), [](const Fact& f) { return f.kind == FactKind::Person; });
    const auto sworn = p.facts.insert(first_person, {FactKind::Person, "hero-sovereign-surface-city-2-212-human_cold",
                                                     {{"name", "Brenbrann"}, {"role", "sovereign"}, {"status", "about"}, {"tie", "liege"}}});
    p.facts.insert(sworn, {FactKind::Deed, "war-3",
                           {{"deed", "war"}, {"part", "credited"}, {"war", "international"}, {"place", "Wenweg (Age 2)"}, {"when", "this age"}}});
    return p;
}

// The same castellan a few years at the post, second in command at another fortress before it, and no war.
inline FactPacket about_a_castellan_second_elsewhere() {
    FactPacket p = about_them_without_a_past();
    for (Fact& f : p.facts)
        if (f.kind == FactKind::Self) {
            f.fields.emplace_back("held_years", "3");
            f.fields.emplace_back("served_at", "the fortress above Mokdorl (Age 2)");
            f.fields.emplace_back("served_as", "second");
        }
    return p;
}

// Asked about themself, a hero whose record says nothing of how they stand where they are: the post they
// keep is how they stand (`them_here`'s {done}).
inline FactPacket about_them_unplaced() {
    FactPacket p = about_them();
    for (Fact& f : p.facts)
        if (f.kind == FactKind::Self) std::erase_if(f.fields, [](const auto& field) { return field.first == "stands"; });
    return p;
}

// Asked about themself, a hero far from home: the castellan's record with home elsewhere.
inline FactPacket about_them_far_from_home() {
    FactPacket p = about_them();
    for (Fact& f : p.facts)
        if (f.kind == FactKind::Self)
            for (auto& [field, value] : f.fields)
                if (field == "home") value = "Linbrig";
    return p;
}

// The same, work the stranger did beside the speaker, where they stand (contract version 14).
inline FactPacket beside(FactPacket p) {
    for (auto& [field, value] : p.facts.back().fields)
        if (field == "beside") value = "yes";
    return p;
}

// Small talk with the castellan once the stranger has finished his work.
inline FactPacket small_talk_after_work() {
    return packet(castellan(), ActKind::SmallTalk, Outcome::Chatted,
                  {{FactKind::Chat, "surface-city-2-212-human_cold",
                    {{"weather", "clear"}, {"wind", "still"}, {"season", "deep winter"}, {"time", "morning"}}}},
                  {.met = "again", .times = 1, .since = "days", .mood = "warm", .work = "finished"});
}

// The same with the worst nest near, as small talk carries it: how things have been since may name it.
inline FactPacket small_talk_after_work_near() {
    FactPacket p = small_talk_after_work();
    p.facts.insert(p.facts.end() - 1, {FactKind::Danger, "nest-hippogryphs-179",
                                       {{"creature", "Hippogryphs"}, {"danger", "3"}, {"family", "fantastic"}, {"range", "walk"}}});
    return p;
}

// The same from a castellan who has fought since they last met (contract version 26): their own deed, a few
// days back, as they last met a few days back.
inline FactPacket small_talk_after_work_and_a_fight(const std::string& state) {
    FactPacket p = small_talk_after_work();
    Fact deed = news_of_a_heros_deed("defended", state, "there");
    for (auto& [key, value] : deed.fields)
        if (key == "since") value = "days";
    p.facts.insert(p.facts.end() - 1, std::move(deed));
    return p;
}

// Contract version 20: the game's account of the castellan's work, as he has it once it is done: Frost
// owls, more than he'd have liked, a few lost, and the stranger held when it counted. `as` "person": what
// came was Ulknord, and a few came with him.
inline FactPacket with_account(FactPacket p, const std::string& as = "nest") {
    Fact told{FactKind::Account, kOwnOffer, {{"outcome", "finished"}}};
    if (as == "person")
        told.fields.insert(told.fields.end(), {{"came", "Ulknord"}, {"came_as", "person"}, {"many", "few"}});
    else
        told.fields.insert(told.fields.end(), {{"came", "Frost owls"}, {"came_as", "nest"}, {"many", "many"}});
    told.fields.insert(told.fields.end(), {{"cost", "few"}, {"part", "held"}});
    p.facts.insert(p.facts.end() - 1, std::move(told));
    return p;
}

// ---- Contract version 25: fame -----------------------------------------------------------------

// Asked about a hero known by a deed: Halbrann, who stood for Brenlin when it fell, told all through the realm.
inline FactPacket about_a_famous_hero() {
    return packet(castellan(), ActKind::AskAboutPerson, Outcome::Answered,
                  {{FactKind::Person, "hero-pretender-surface-city-0-243-human_cold",
                    {{"name", "Halbrann"}, {"role", "pretender"}, {"where", "the ruins of Linbrig"}, {"status", "about"}, {"fame", "renowned"}}},
                   {FactKind::Deed, "ruin-brenlin",
                    {{"deed", "ruin"}, {"part", "credited"}, {"place", "the ruins of Brenlin"}, {"when", "the last age"}, {"scope", "realm"}}}});
}

// Asked about the speaker's own sovereign, known all through these parts for a war won.
inline FactPacket about_the_sovereign() {
    return packet(gate_captain(), ActKind::AskAboutPerson, Outcome::Answered,
                  {{FactKind::Person, "hero-sovereign-surface-city-0-178-human_cold",
                    {{"name", "Aldomir"}, {"role", "sovereign"}, {"where", "Wenweg"}, {"status", "about"}, {"fame", "legendary"},
                     {"sovereign", "yes"}}},
                   {FactKind::Deed, "war-9",
                    {{"deed", "war"}, {"part", "credited"}, {"war", "international"}, {"against", "Linbrig"}, {"when", "this age"},
                     {"scope", "regional"}}}});
}

// ---- Contract version 24: the stranger's name ------------------------------------------------

// Told a name: taken, by a speaker cold to the stranger, by a frightened one; told it again; told another.
inline FactPacket introduced(const std::string& mood = "neutral", const std::string& feeling = "calm") {
    return packet(castellan(), ActKind::Introduce, Outcome::Introduced, {},
                  {.mood = mood, .turn = 2, .feeling = feeling, .called = "Aldric", .called_how = "told"});
}

inline FactPacket known_name() {
    return packet(castellan(), ActKind::Introduce, Outcome::Known, {},
                  {.met = "again", .times = 1, .since = "days", .turn = 2, .called = "Aldric", .called_how = "told"});
}

inline FactPacket renamed() {
    return packet(castellan(), ActKind::Introduce, Outcome::Renamed, {},
                  {.met = "again", .times = 1, .since = "days", .turn = 2, .called = "Brenna", .called_how = "told",
                   .called_other = "Aldric"});
}

// Greeted at a first meeting by someone who heard the stranger's name: by word of their deed, or overheard.
inline FactPacket greeted_first_named(const std::string& how = "word") {
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {}, {.called = "Aldric", .called_how = how});
}

// Greeted and seen off by someone who knows the name.
inline FactPacket greeted_again_named() {
    return packet(castellan(), ActKind::Greet, Outcome::Greeted, {},
                  {.met = "again", .times = 1, .since = "days", .called = "Aldric", .called_how = "told"});
}

inline FactPacket parted_named() {
    return packet(castellan(), ActKind::Farewell, Outcome::Parted, {}, {.turn = 3, .called = "Aldric", .called_how = "told"});
}

inline std::vector<FactPacket> conversation() {
    return {about_them(),
            greeted_after_own_deed("won"),
            greeted_after_own_deed("lost"),
            small_talk_with_a_heros_deed(),
            small_talk_with_a_heros_deed("sovereign"),
            small_talk_with_a_heros_deed("ally", "lost"),
            about_a_famous_hero(),
            about_the_sovereign(),
            introduced(),
            introduced("hostile"),
            introduced("neutral", "afraid"),
            known_name(),
            renamed(),
            greeted_first_named(),
            greeted_first_named("overheard"),
            greeted_again_named(),
            parted_named(),
            about_them_far_from_home(),
            about_them_unplaced(),
            about_them_without_rivals(),
            about_them_after_a_fight(),
            about_them_after_a_fight("won"),
            about_them_without_a_past(),
            about_a_castellan_with_a_past(),
            about_a_castellan_second_elsewhere(),
            small_talk_after_work(),
            small_talk_after_work_near(),
            small_talk_after_work_and_a_fight("won"),
            small_talk_after_work_and_a_fight("lost"),
            beside(small_talk_after_work()),
            beside(greeted_after("finished", "warm")),
            greeted_first(),
            greeted_again(),
            greeted_warmly(),
            greeted_coldly(),
            greeted_after("taken", "friendly"),
            greeted_after("finished", "warm"),
            greeted_after("failed", "neutral"),
            greeted_after("abandoned", "neutral"),
            with_account(greeted_after("finished", "warm")),
            greeted_with_a_question(),
            apology_after("My brother came with me. He didn't make it back."),
            apology_after("My brother came with me. He didn't make it back.", Outcome::Forgiven),
            with_account(beside(greeted_after("finished", "warm")), "person"),
            with_account(small_talk_after_work()),
            with_account(beside(small_talk_after_work())),
            small_talk("clear", "breezy"),
            small_talk("unsettled", "windy"),
            small_talk("stormy", "a gale"),
            small_talk("fair", "still", "hostile"),
            small_talk("fair", "breezy", "friendly", "the turn of the season", "afternoon"),
            parted_coldly(),
            parted_holding_work(),
            beside(parted_holding_work()),
            forgiven("neutral"),
            forgiven("hostile"),
            unwronged(),
            about_the_town(),
            about_the_fortress(),
            acknowledged(),
            acknowledged_with("taken"),
            acknowledged_with("offered"),
            acknowledged_with("finished"),
            thanked_with_work("taken"),
            offended_after("finished"),
            thanked(),
            thanked("hostile"),
            offended(false),
            offended(true),
            heard(),
            safety_unsafe(),
            safety_wary(),
            safety_safe(),
            surroundings_again(),
            exposed_mask(),
            exposed_beast(),
            exposed_heart(),
            denied(false),
            denied(true),
            // Contract version 9: calming words, and a frightened speaker.
            calmed("afraid"),
            calmed("calm"),
            still_afraid(),
            still_afraid("hostile"),
            not_afraid(),
            not_afraid("uneasy"),
            frightened(ActKind::Greet, Outcome::Greeted, "afraid"),
            frightened(ActKind::Greet, Outcome::Greeted, "panicked"),
            frightened(ActKind::SmallTalk, Outcome::Chatted, "afraid"),
            frightened(ActKind::SmallTalk, Outcome::Chatted, "panicked"),
            frightened(ActKind::Thank, Outcome::Thanked, "afraid"),
            frightened(ActKind::Thank, Outcome::Thanked, "panicked"),
            frightened(ActKind::Farewell, Outcome::Parted, "afraid"),
            frightened(ActKind::Farewell, Outcome::Parted, "panicked"),
            frightened(ActKind::AskSurroundings, Outcome::Answered, "afraid"),
            frightened(ActKind::AskSurroundings, Outcome::Answered, "panicked"),
            // Contract version 10.
            not_afraid_eased(),
            greeted_eased(),
            joined_surroundings(),
            // Contract version 11.
            greeted_having_heard(),
            greeted_having_heard_of("failed"),
            greeted_having_heard_of("given_up"),
            small_talk_with_news(),
            small_talk_with_word_of("failed", true),
            small_talk_with_word_of("given_up", true),
            small_talk_with_word_of("asked", false),
            about_the_town_with_news(),
            // Contract version 12: the words kept a clause, a fragment, and opening with a name.
            greeted_remembering(),
            greeted_remembering("To the coast, by the high road."),
            greeted_remembering("Dunmoor is where I was born."),
            about_the_town_told()};
}

}  // namespace packets
