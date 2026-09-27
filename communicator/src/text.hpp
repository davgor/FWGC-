// Small text helpers the interpreter, the voice and Known share.
#pragma once

#include <string>
#include <string_view>

namespace communicator::text {

// One line of plain text: a control character becomes a space, and the ends are trimmed.
std::string one_line(std::string_view text);

// A snake_case key or value as words: "gate_captain" -> "gate captain".
std::string spaced(std::string_view text);

// Text as it is said aloud: one line, without any part in brackets ("Wenweg (Age 2)" ->
// "Wenweg"), and single spaces.
std::string spoken(std::string_view text);

// `text` inside a JSON string: quotes and backslashes escaped.
std::string json_escaped(std::string_view text);

// `text` with typographic punctuation made plain: curly quotes become straight ones, dashes a
// hyphen, the ellipsis three dots. Words then split the same whichever a keyboard typed.
std::string plain_punctuation(std::string_view text);

// Throws Error for a line longer than kMaxLineBytes.
void check_line(std::string_view line);

// A packet's step on a scale of 1 to 5, in a word: difficulty "very easy" to "very hard",
// threat or danger "slight" to "deadly". Empty for any other key, or a value that is not 1 to 5.
std::string scale_word(std::string_view key, std::string_view value);

// How far a packet's `range` is, in words (orchestrator/contract.hpp publishes the bands):
// "here" is "right here", "near" "a short walk away", "distant" "a long way off". Empty for a
// band the contract does not publish.
std::string range_words(std::string_view range);

// Whether a fact's field is for the game, not for saying: the exact marker of a piece of work.
bool for_the_game(std::string_view key);

// What an offer asks, from its stated purpose: the order after any reason given first. "Our
// fields lie in its reach: drive off the wyrm." -> "drive off the wyrm.". Every purpose the
// generator writes is an order (2026-09-24), so the ask begins with its verb.
std::string ask_of(const std::string& purpose);

// A phrase from a packet's text, set in mid-sentence: its first letter lowered, its end stop
// dropped. "Hold the fortress with me." -> "hold the fortress with me".
std::string mid_sentence(std::string text);

// A giver's words retold by someone else: "with me" is "with them" in another's mouth.
std::string retold(const std::string& text);

}  // namespace communicator::text
