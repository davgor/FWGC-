#include "lex.hpp"

#include <algorithm>
#include <iterator>
#include <cctype>

namespace layers {

namespace {

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool is_upper(char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }
bool is_lower(char c) { return std::islower(static_cast<unsigned char>(c)) != 0; }

void blank(std::string& s, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to && i < s.size(); ++i)
        if (s[i] != '\n') s[i] = ' ';
}

// End of a pp-number starting at `i`: digits, letters, '.', a digit separator between two
// digit-ish characters, and a sign after an exponent letter.
std::size_t number_end(std::string_view text, std::size_t i) {
    std::size_t j = i + 1;
    while (j < text.size()) {
        const char d = text[j];
        if (ident_char(d) || d == '.') {
            ++j;
            continue;
        }
        if (d == '\'' && j + 1 < text.size() && ident_char(text[j + 1])) {
            j += 2;
            continue;
        }
        const char p = text[j - 1];
        if ((d == '+' || d == '-') && (p == 'e' || p == 'E' || p == 'p' || p == 'P')) {
            ++j;
            continue;
        }
        break;
    }
    return j;
}

bool raw_prefix(std::string_view word) {
    return word == "R" || word == "u8R" || word == "uR" || word == "UR" || word == "LR";
}

// Keywords that name a type, so an identifier after one is being declared, not called.
bool type_keyword(std::string_view word) {
    static constexpr std::string_view kTypes[] = {
        "void", "bool", "char", "char8_t", "char16_t", "char32_t", "wchar_t", "short", "int",
        "long", "signed", "unsigned", "float", "double", "auto", "const", "volatile"};
    return std::find(std::begin(kTypes), std::end(kTypes), word) != std::end(kTypes);
}

bool declares(const Token& prev) {
    if (prev.kind == TokenKind::Identifier) return !is_keyword(prev.text) || type_keyword(prev.text);
    return prev.text == ">" || prev.text == "*" || prev.text == "&";
}

bool is_open_paren(const std::vector<Token>& tokens, std::size_t t) {
    return t < tokens.size() && tokens[t].kind == TokenKind::Punct && tokens[t].text == "(";
}

}  // namespace

bool is_keyword(std::string_view word) {
    static constexpr std::string_view kKeywords[] = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
        "case", "catch", "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept",
        "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await",
        "co_return", "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast",
        "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
        "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register",
        "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
        "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
        "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq", "import", "module",
        "final", "override", "defined"};
    return std::find(std::begin(kKeywords), std::end(kKeywords), word) != std::end(kKeywords);
}

Stripped strip_cpp(std::string_view text) {
    Stripped out{std::string(text), std::string(text)};
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = text[i];
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            std::size_t j = i + 2;
            while (j < n) {
                if (text[j] == '\n') {
                    std::size_t back = j;
                    if (back > 0 && text[back - 1] == '\r') --back;
                    if (back > 0 && text[back - 1] == '\\') {  // a spliced line continues the comment
                        ++j;
                        continue;
                    }
                    break;
                }
                ++j;
            }
            blank(out.code, i, j);
            blank(out.no_comments, i, j);
            i = j;
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            std::size_t j = text.find("*/", i + 2);
            j = (j == std::string_view::npos) ? n : j + 2;
            blank(out.code, i, j);
            blank(out.no_comments, i, j);
            i = j;
            continue;
        }
        if (is_digit(c) && (i == 0 || !ident_char(text[i - 1]))) {
            i = number_end(text, i);
            continue;
        }
        if (ident_start(c)) {
            std::size_t j = i + 1;
            while (j < n && ident_char(text[j])) ++j;
            if (j < n && text[j] == '"' && raw_prefix(text.substr(i, j - i))) {
                const std::size_t paren = text.find('(', j + 1);
                if (paren != std::string_view::npos && paren - (j + 1) <= 16) {
                    const std::string delimiter = ")" + std::string(text.substr(j + 1, paren - j - 1)) + "\"";
                    const std::size_t end = text.find(delimiter, paren + 1);
                    const std::size_t stop = end == std::string_view::npos ? n : end + delimiter.size();
                    blank(out.code, j, stop);
                    i = stop;
                    continue;
                }
            }
            i = j;
            continue;
        }
        if (c == '"' || c == '\'') {
            std::size_t j = i + 1;
            while (j < n && text[j] != c && text[j] != '\n') j += (text[j] == '\\' && j + 1 < n) ? 2 : 1;
            const std::size_t stop = (j < n && text[j] == c) ? j + 1 : j;
            blank(out.code, i, stop);
            i = stop;
            continue;
        }
        ++i;
    }
    return out;
}

std::string strip_cmake(std::string_view text) {
    std::string out(text);
    const std::size_t n = text.size();
    bool quoted = false;
    std::size_t i = 0;
    while (i < n) {
        const char c = text[i];
        if (quoted) {
            if (c == '\\') {
                i += 2;
                continue;
            }
            if (c == '"') quoted = false;
            ++i;
            continue;
        }
        if (c == '"') {
            quoted = true;
            ++i;
            continue;
        }
        if (c == '#') {
            std::size_t stop = std::string_view::npos;
            if (i + 1 < n && text[i + 1] == '[') {  // bracket comment: #[[ ]] or #[==[ ]==]
                std::size_t k = i + 2;
                std::size_t equals = 0;
                while (k < n && text[k] == '=') {
                    ++equals;
                    ++k;
                }
                if (k < n && text[k] == '[') {
                    const std::string close = "]" + std::string(equals, '=') + "]";
                    const std::size_t end = text.find(close, k + 1);
                    stop = end == std::string_view::npos ? n : end + close.size();
                }
            }
            if (stop == std::string_view::npos) {
                stop = text.find('\n', i);
                if (stop == std::string_view::npos) stop = n;
            }
            blank(out, i, stop);
            i = stop;
            continue;
        }
        ++i;
    }
    return out;
}

int line_of(std::string_view text, std::size_t offset) {
    offset = std::min(offset, text.size());
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
}

std::vector<Include> includes_of(std::string_view no_comments) {
    std::vector<Include> out;
    int line = 1;
    std::size_t start = 0;
    while (start <= no_comments.size()) {
        std::size_t end = no_comments.find('\n', start);
        if (end == std::string_view::npos) end = no_comments.size();
        const std::string_view l = no_comments.substr(start, end - start);
        std::size_t k = 0;
        auto skip_space = [&] {
            while (k < l.size() && (l[k] == ' ' || l[k] == '\t')) ++k;
        };
        skip_space();
        if (k < l.size() && l[k] == '#') {
            ++k;
            skip_space();
            if (l.substr(k, 7) == "include") {
                k += 7;
                if (l.substr(k, 5) == "_next") k += 5;
                skip_space();
                if (k < l.size() && (l[k] == '<' || l[k] == '"')) {
                    const char close = l[k] == '<' ? '>' : '"';
                    const std::size_t stop = l.find(close, k + 1);
                    if (stop != std::string_view::npos)
                        out.push_back({line, std::string(l.substr(k + 1, stop - k - 1)), close == '>'});
                }
            }
        }
        if (end == no_comments.size()) break;
        start = end + 1;
        ++line;
    }
    return out;
}

std::vector<Token> tokenize(std::string_view code) {
    std::vector<Token> out;
    const std::size_t n = code.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = code[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (ident_start(c)) {
            std::size_t j = i + 1;
            while (j < n && ident_char(code[j])) ++j;
            out.push_back({TokenKind::Identifier, std::string(code.substr(i, j - i)), i});
            i = j;
            continue;
        }
        if (is_digit(c)) {
            i = number_end(code, i);
            continue;
        }
        if (c == ':' && i + 1 < n && code[i + 1] == ':') {
            out.push_back({TokenKind::Punct, "::", i});
            i += 2;
            continue;
        }
        if (c == '-' && i + 1 < n && code[i + 1] == '>') {
            out.push_back({TokenKind::Punct, "->", i});
            i += 2;
            continue;
        }
        out.push_back({TokenKind::Punct, std::string(1, c), i});
        ++i;
    }
    return out;
}

std::vector<std::string> words_of(std::string_view identifier) {
    std::vector<std::string> words;
    std::string current;
    auto flush = [&] {
        if (current.empty()) return;
        for (char& ch : current) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        words.push_back(current);
        current.clear();
    };
    for (std::size_t k = 0; k < identifier.size(); ++k) {
        const char c = identifier[k];
        if (c == '_') {
            flush();
            continue;
        }
        if (is_upper(c) && k > 0) {
            const char prev = identifier[k - 1];
            const bool after_lower = is_lower(prev) || is_digit(prev);
            const bool acronym_end = is_upper(prev) && k + 1 < identifier.size() && is_lower(identifier[k + 1]);
            if (after_lower || acronym_end) flush();
        }
        current.push_back(c);
    }
    flush();
    return words;
}

std::set<std::string> declared_functions(const std::vector<Token>& tokens, bool namespace_scope_only) {
    std::set<std::string> out;
    std::vector<bool> namespace_brace;
    std::size_t statement_start = 0;
    for (std::size_t t = 0; t < tokens.size(); ++t) {
        const Token& token = tokens[t];
        if (token.kind == TokenKind::Punct) {
            if (token.text == "{") {
                bool is_namespace = false;
                for (std::size_t u = statement_start; u < t; ++u) {
                    if (tokens[u].kind != TokenKind::Identifier) continue;
                    // extern "C" { -- the literal is blanked, so extern sits right before the brace.
                    if (tokens[u].text == "namespace" || (tokens[u].text == "extern" && u + 1 == t)) {
                        is_namespace = true;
                        break;
                    }
                }
                namespace_brace.push_back(is_namespace);
                statement_start = t + 1;
            } else if (token.text == "}") {
                if (!namespace_brace.empty()) namespace_brace.pop_back();
                statement_start = t + 1;
            } else if (token.text == ";") {
                statement_start = t + 1;
            }
            continue;
        }
        if (t == 0 || !is_open_paren(tokens, t + 1) || is_keyword(token.text)) continue;
        if (!declares(tokens[t - 1])) continue;
        const bool at_namespace_scope =
            std::all_of(namespace_brace.begin(), namespace_brace.end(), [](bool b) { return b; });
        if (!namespace_scope_only || at_namespace_scope) out.insert(token.text);
    }
    return out;
}

std::vector<Call> unqualified_calls(const std::vector<Token>& tokens) {
    std::vector<Call> out;
    for (std::size_t t = 0; t < tokens.size(); ++t) {
        const Token& token = tokens[t];
        if (token.kind != TokenKind::Identifier || !is_open_paren(tokens, t + 1) || is_keyword(token.text)) continue;
        if (t > 0) {
            const Token& prev = tokens[t - 1];
            if (prev.text == "." || prev.text == "->" || prev.text == "::") continue;
            if (declares(prev)) continue;
        }
        out.push_back({token.text, token.offset});
    }
    return out;
}

}  // namespace layers
