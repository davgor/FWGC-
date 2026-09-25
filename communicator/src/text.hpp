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

}  // namespace communicator::text
