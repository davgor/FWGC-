// Pieces of GBNF, the grammar language inference::Sampling takes (llama.cpp's
// grammars/README.md).
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace communicator::gbnf {

// `text` as a GBNF string literal: quoted, with backslashes, quotes and control characters
// escaped. UTF-8 passes through as it is.
std::string literal(std::string_view text);

// One of `words`, exactly, as a GBNF expression. Shared beginnings are written once (a trie), so
// the grammar holds few alternatives open at a time. Characters are UTF-8 code points, never
// split. Empty words are dropped; with no word at all, the result is empty.
std::string one_of(std::vector<std::string> words);

// A lower-case word, whole, that no entry of `banned` covers unless it is one of `allowed`, as a
// GBNF expression. Its first letter is a-z; each letter after it is a-z or one of `more_letters`
// (a character-class body, such as "\\u00DF-\\u00FF"). `rest` names a rule matching any run of
// those later letters.
//
// An entry covers the word it spells, so "gold" cannot be written, and "golden" and "gol" can.
// An entry ending in "*" is a stem, and covers every word that begins with it: "north*" bars
// "north", "northern" and "northwardstretch". A word of `allowed` may be written though an entry
// covers it, and only that word: allowing "southern" leaves "southward" barred. Entries and
// allowed words hold only a-z (and a stem its final "*"); others are dropped. With every word
// covered, the result is empty.
//
// It is the complement of the entries' trie. At each letter the word either follows an entry
// further, or leaves every one of them and runs on freely. Past a stem, it may only spell out
// an allowed word.
std::string word_except(const std::vector<std::string>& banned, const std::vector<std::string>& allowed,
                        std::string_view more_letters, std::string_view rest);

}  // namespace communicator::gbnf
