// Just enough C++ lexing to check layer rules without a compiler.
//
// Nothing here parses C++. It blanks what must not be read as code -- comments, and string and
// character literals -- keeping every newline, so an offset into the result is an offset into
// the file and line numbers survive. On top of that it offers the handful of token views the
// rules need: includes, identifiers, the identifier-then-paren pattern, and a namespace-aware
// brace depth.
#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace layers {

struct Stripped {
    std::string code;         // comments and literals blanked
    std::string no_comments;  // comments blanked, literals kept (for #include "...")
};

// Blanks with spaces, keeps '\n'. Handles //, /* */, "..." with escapes, '...' (and the digit
// separator in 1'000), raw strings R"d(...)d" with any encoding prefix. A plain literal that
// reaches a newline unterminated ends there, so one stray quote cannot blank the file.
Stripped strip_cpp(std::string_view text);

// '#' comments blanked (outside double quotes); for CMake files.
std::string strip_cmake(std::string_view text);

// 1-based line of a byte offset.
int line_of(std::string_view text, std::size_t offset);

struct Include {
    int line = 0;
    std::string path;
    bool angled = false;
};
// From strip_cpp's no_comments text.
std::vector<Include> includes_of(std::string_view no_comments);

enum class TokenKind { Identifier, Punct };
struct Token {
    TokenKind kind;
    std::string text;
    std::size_t offset;
};
// Identifiers ([A-Za-z_][A-Za-z0-9_]*) and punctuation, with '::' and '->' as single tokens.
// Numbers are skipped. Run on strip_cpp's code text.
std::vector<Token> tokenize(std::string_view code);

// An identifier split into lower-case words at '_' and at case changes:
// kPacingPhase -> k pacing phase, LLMClient -> llm client, utf8Name -> utf8 name.
std::vector<std::string> words_of(std::string_view identifier);

// Names that appear in a declaration position: an identifier directly followed by '(' and
// directly preceded by a type-ish token (an identifier that is not a keyword, or '>', '*',
// '&'). With namespace_scope_only, only where every enclosing brace is a namespace (or
// extern "C") brace: free functions, not members or locals.
std::set<std::string> declared_functions(const std::vector<Token>& tokens, bool namespace_scope_only);

struct Call {
    std::string name;
    std::size_t offset;
};
// Unqualified calls: an identifier directly followed by '(' whose previous token is not '.',
// '->' or '::' and is not a declaration's type. Keywords are not calls.
std::vector<Call> unqualified_calls(const std::vector<Token>& tokens);

bool is_keyword(std::string_view word);

}  // namespace layers
