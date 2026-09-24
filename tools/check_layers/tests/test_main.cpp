// The checker's tests: the lexer's and the JSON reader's corner cases, then every fixture
// under tests/fixtures.
//
// A fixture is a folder with a tree/ to check and expect.txt, one expected finding per line
// as "<rule> <path>[:<line>]". The found set must equal the expected set exactly. A fixture
// named bad_* must expect findings (a bad fixture that expects nothing would pass against a
// checker that finds nothing), and one named clean_* must expect none.
//
// Folders starting with '_' are bases. A fixture with base.txt is checked on a copy of that
// base's tree with its own tree laid over it, and uses the base's layers.txt unless it has
// its own. clean_base holds the base itself to no findings, so every bad_ fixture's findings
// are its own. A fixture with snapshot.txt is checked as the commit gate checks a staged tree:
// without skipping the ignore-dir names.
//
// usage: layers_tests <fixtures-dir> [<layers.txt to parse>...]
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "json.hpp"
#include "lex.hpp"
#include "rules.hpp"

namespace fs = std::filesystem;

namespace {

int failures = 0;
int checks = 0;

void expect(bool condition, const std::string& what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cout << "FAIL: " << what << '\n';
    }
}

bool contains(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

void lexer_tests() {
    {
        const std::string text =
            "int a = 1'000; // a comment with plan_city(\n"
            "char c = '\\'';\n"
            "const char* s = \"x // not a comment ( plan_city\";\n"
            "auto r = R\"d(raw \" ) plan_city)d\";\n"
            "int b = 0x1F'FF;\n";
        const layers::Stripped s = layers::strip_cpp(text);
        expect(s.code.size() == text.size(), "strip_cpp keeps the length");
        expect(std::count(s.code.begin(), s.code.end(), '\n') == 5, "strip_cpp keeps every newline");
        expect(!contains(s.code, "plan_city"), "comments, strings and raw strings are blanked from code");
        expect(contains(s.code, "int b"), "the line after a raw string is code again");
        expect(contains(s.no_comments, "not a comment"), "no_comments keeps string literals");
        expect(!contains(s.no_comments, "a comment with"), "no_comments blanks comments");
    }
    {
        const std::string text = "#error don't do this\nint after_the_stray_quote;\n";
        const layers::Stripped s = layers::strip_cpp(text);
        expect(contains(s.code, "after_the_stray_quote"), "an unterminated quote ends at its newline");
    }
    {
        const std::string text =
            "#include \"fwg/api/world.hpp\"\n"
            "  #  include <vector>\n"
            "// #include \"fwg/tectonics.hpp\"\n"
            "/* #include \"fwg/climate.hpp\" */\n";
        const std::vector<layers::Include> inc = layers::includes_of(layers::strip_cpp(text).no_comments);
        expect(inc.size() == 2, "includes_of finds the two live includes and not the commented ones");
        expect(inc.size() == 2 && inc[0].path == "fwg/api/world.hpp" && inc[0].line == 1 && !inc[0].angled,
               "the quoted include, on line 1");
        expect(inc.size() == 2 && inc[1].path == "vector" && inc[1].line == 2 && inc[1].angled, "the angled include, on line 2");
    }
    {
        using W = std::vector<std::string>;
        expect(layers::words_of("kPacingPhase") == W{"k", "pacing", "phase"}, "words_of kPacingPhase");
        expect(layers::words_of("LLMClient") == W{"llm", "client"}, "words_of LLMClient");
        expect(layers::words_of("settlement_spacing") == W{"settlement", "spacing"}, "words_of settlement_spacing");
        expect(layers::words_of("utf8Name") == W{"utf8", "name"}, "words_of utf8Name");
        expect(layers::words_of("create_directory") == W{"create", "directory"}, "words_of create_directory");
    }
    {
        const std::string text =
            "namespace fwg {\n"
            "int free_function(int);\n"
            "struct S { void member(); };\n"
            "namespace detail { double nested_free(); }\n"
            "inline bool defined_inline(int x) { return helper_call(x); }\n"
            "}\n"
            "extern \"C\" { void c_function(void); }\n"
            "void global_function();\n";
        const std::vector<layers::Token> t = layers::tokenize(layers::strip_cpp(text).code);
        const std::set<std::string> ns = layers::declared_functions(t, true);
        expect(ns == std::set<std::string>{"free_function", "nested_free", "defined_inline", "c_function", "global_function"},
               "declared_functions at namespace scope: free functions only");
        const std::set<std::string> all = layers::declared_functions(t, false);
        expect(all.count("member") == 1, "declared_functions at any scope includes members");
        expect(all.count("helper_call") == 0, "a call inside a body is not a declaration");
    }
    {
        const std::string text =
            "x = plan_city(a);\n"
            "obj.plan_city(b);\n"
            "ptr->plan_city(c);\n"
            "fwg::plan_city(d);\n"
            "int plan_city(int);\n"
            "return census(e);\n";
        const std::vector<layers::Call> calls = layers::unqualified_calls(layers::tokenize(layers::strip_cpp(text).code));
        std::vector<std::string> names;
        for (const layers::Call& c : calls) names.push_back(c.name);
        expect(names == std::vector<std::string>{"plan_city", "census"},
               "unqualified_calls: the bare call and the returned one, not members, qualified calls or declarations");
    }
    {
        const std::string text = "add_subdirectory(../generator fwg) # ../orchestrator\n#[[ ../viewer ]]\nset(X \"# not a comment\")\n";
        const std::string code = layers::strip_cmake(text);
        expect(contains(code, "../generator"), "strip_cmake keeps code");
        expect(!contains(code, "../orchestrator") && !contains(code, "../viewer"), "strip_cmake blanks # and bracket comments");
        expect(contains(code, "# not a comment"), "strip_cmake keeps a # inside quotes");
    }
}

void json_tests() {
    namespace json = layers::json;
    {
        const std::string text =
            "{\"verdict\":\"PASS\",\"n\":-1.5e3,\"ok\":true,\"none\":null,"
            "\"s\":\"a\\\"b\\\\c\\n\\u00e9\\ud83d\\ude00\",\"list\":[1,[],{}]}";
        const json::Value v = json::parse(text);
        expect(v.get("verdict") && v.get("verdict")->text == "PASS", "json: a string member");
        expect(v.get("n") && v.get("n")->number() == -1500.0, "json: a number keeps its value");
        expect(v.get("ok") && v.get("ok")->boolean, "json: true");
        expect(v.get("none") && v.get("none")->is(json::Value::Type::Null), "json: null");
        expect(v.get("s") && v.get("s")->text == "a\"b\\c\n\xc3\xa9\xf0\x9f\x98\x80",
               "json: escapes, a BMP escape and a surrogate pair decode to UTF-8");
        expect(json::dump(json::parse(json::dump(v))) == json::dump(v), "json: dump then parse is stable");
    }
    auto refused = [](const std::string& text) {
        try {
            layers::json::parse(text);
        } catch (const layers::json::Error&) {
            return true;
        }
        return false;
    };
    expect(refused("{\"a\":1,}"), "json: a trailing comma is refused");
    expect(refused("{\"a\":1} x"), "json: trailing characters are refused");
    expect(refused("\"\\ud83d\""), "json: a lone surrogate is refused");
    expect(refused("[1, 2"), "json: an unterminated array is refused");
}

struct Expected {
    std::set<std::string> keys;
    bool readable = true;
};

std::string key_of(const layers::Finding& f) {
    return f.rule + " " + f.path + (f.line > 0 ? ":" + std::to_string(f.line) : "");
}

Expected read_expect(const fs::path& file) {
    Expected e;
    std::string text;
    try {
        text = layers::read_file(file);
    } catch (const std::exception&) {
        e.readable = false;
        return e;
    }
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::vector<std::string> w = layers::split_words(line);
        if (w.size() == 2) e.keys.insert(w[0] + " " + w[1]);
        else if (!w.empty()) e.keys.insert("MALFORMED " + line);
        start = end + 1;
    }
    return e;
}

std::string first_word(const fs::path& file) {
    const std::vector<std::string> w = layers::split_words(layers::read_file(file));
    return w.empty() ? std::string{} : w[0];
}

// The tree a fixture is checked on: its own, or a scratch copy of its base with it laid over.
fs::path prepare(const fs::path& fixture, const fs::path& dir, fs::path& layers_file) {
    layers_file = fixture / "layers.txt";
    if (!fs::exists(fixture / "base.txt")) return fixture / "tree";
    const fs::path base = dir / first_word(fixture / "base.txt");
    if (!fs::exists(layers_file)) layers_file = base / "layers.txt";
    const fs::path work = fs::temp_directory_path() / "fwgc_layers_tests" / fixture.filename();
    fs::remove_all(work);
    fs::create_directories(work);
    fs::copy(base / "tree", work, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    if (fs::exists(fixture / "tree"))
        fs::copy(fixture / "tree", work, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    return work;
}

void fixture_tests(const fs::path& dir) {
    std::vector<fs::path> fixtures;
    for (const fs::directory_entry& e : fs::directory_iterator(dir))
        if (e.is_directory() && e.path().filename().string()[0] != '_') fixtures.push_back(e.path());
    std::sort(fixtures.begin(), fixtures.end());
    expect(!fixtures.empty(), "at least one fixture under " + dir.generic_string());
    for (const fs::path& f : fixtures) {
        const std::string name = f.filename().string();
        const Expected expected = read_expect(f / "expect.txt");
        expect(expected.readable, name + ": has expect.txt");
        if (name.rfind("bad_", 0) == 0) expect(!expected.keys.empty(), name + ": a bad_ fixture expects findings");
        else if (name.rfind("clean_", 0) == 0) expect(expected.keys.empty(), name + ": a clean_ fixture expects none");
        else expect(false, name + ": a fixture is named bad_* or clean_*");
        std::set<std::string> actual;
        std::vector<layers::Finding> found;
        try {
            fs::path layers_file;
            layers::Inputs in;
            in.root = prepare(f, dir, layers_file);
            in.rules = layers::parse_rules(layers::read_file(layers_file), layers_file.generic_string());
            in.consumer_api = layers::load_api_lists(in.rules, in.root);
            in.own_api = in.consumer_api;
            in.skip_ignored_dirs = !fs::exists(f / "snapshot.txt");
            found = layers::check_tree(in);
        } catch (const std::exception& e) {
            expect(false, name + ": threw " + e.what());
            continue;
        }
        for (const layers::Finding& x : found) actual.insert(key_of(x));
        for (const std::string& k : expected.keys)
            expect(actual.count(k) == 1, name + ": expected " + k);
        for (const layers::Finding& x : found)
            expect(expected.keys.count(key_of(x)) == 1, name + ": unexpected " + layers::format(x));
    }
    std::cout << fixtures.size() << " fixture(s)\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: layers_tests <fixtures-dir> [<layers.txt>...]\n";
        return 2;
    }
    lexer_tests();
    json_tests();
    fixture_tests(argv[1]);
    for (int i = 2; i < argc; ++i) {
        try {
            layers::parse_rules(layers::read_file(argv[i]), argv[i]);
            expect(true, "");
        } catch (const std::exception& e) {
            expect(false, std::string("the rules file parses: ") + e.what());
        }
    }
    std::cout << checks << " check(s), " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
