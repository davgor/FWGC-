// The brain database's plumbing: one in-memory SQLite connection with the schema, statements
// prepared once and kept, transactions, and the read-only runner a game's query goes through.
// What a brain holds is the director's (director.cpp); the tables are README.md's.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "orchestrator/director.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace orchestrator {

struct Brains::Handle {
    sqlite3* db = nullptr;
    std::map<std::string, sqlite3_stmt*, std::less<>> prepared;  // by their SQL

    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle();
};

namespace brains {

// The schema's version: bumped with any change to a table or view a game may read.
inline constexpr int kSchemaVersion = 6;

// A game's query stops after this many of SQLite's steps (about a second's work) and when it
// answers more rows than this.
inline constexpr int kQueryStepsInThousands = 200000;
inline constexpr std::size_t kQueryRows = 100000;

// One statement, taken from the connection's prepared ones (prepared on first use) and reset when
// this goes. Never two at once of the same SQL.
class Statement {
public:
    Statement(Brains::Handle& handle, std::string_view sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& text(int index, std::string_view value);
    // NULL for empty text.
    Statement& text_or_null(int index, std::string_view value);
    Statement& integer(int index, std::int64_t value);
    Statement& real(int index, double value);
    Statement& maybe(int index, const std::optional<int>& value);

    // Steps once: true with a row to read, false when done.
    bool step();
    // Steps to the end.
    void run();

    std::int64_t integer(int column) const;
    double real(int column) const;
    std::string text(int column) const;
    std::optional<int> maybe(int column) const;
    bool null(int column) const;

private:
    sqlite3* db_;
    sqlite3_stmt* statement_;
};

// Everything between here and commit() happens, or none of it does.
class Transaction {
public:
    explicit Transaction(Brains::Handle& handle);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();

private:
    Brains::Handle& handle_;
    bool done_ = false;
};

// A game's query: one statement that only reads, under an authorizer that lets through reading
// and nothing else. Throws Error for anything else, for a statement SQLite will not take, and for
// one that runs past kQueryStepsInThousands or answers more than kQueryRows rows.
QueryResult read_only(Brains::Handle& handle, std::string_view sql);

}  // namespace brains
}  // namespace orchestrator
