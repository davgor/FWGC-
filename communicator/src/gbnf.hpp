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

}  // namespace communicator::gbnf
