#include "brains.hpp"

#include <stdexcept>
#include <utility>

#include "sqlite3.h"

namespace orchestrator {

namespace {

// The tables a game reads (README.md, "The brain database"). `people`, `places`, `work` and
// `relationships` are copies the director writes afresh before a query when what they copy has
// changed; `events`, `event_states`, `facts`, `accounts`, `exchanges` (with `exchange_facts`) and
// `memories` are the brains themselves, written by the inputs.
constexpr const char* kSchema = R"sql(
CREATE TABLE people (
  uid TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  kind TEXT NOT NULL,
  role TEXT NOT NULL,
  site TEXT,
  node INTEGER,
  present INTEGER NOT NULL
) WITHOUT ROWID;
CREATE INDEX people_node ON people (node);
CREATE INDEX people_site ON people (site);
CREATE TABLE places (
  id TEXT PRIMARY KEY,
  kind TEXT NOT NULL,
  name TEXT NOT NULL,
  node INTEGER NOT NULL
) WITHOUT ROWID;
CREATE INDEX places_node ON places (node);
CREATE TABLE work (
  quest_id TEXT PRIMARY KEY,
  giver TEXT,
  verb TEXT,
  purpose TEXT,
  anchor TEXT,
  node INTEGER,
  difficulty INTEGER,
  state TEXT NOT NULL,
  offered_day REAL NOT NULL,
  expires_day REAL NOT NULL,
  closed_day REAL,
  closed_reason TEXT
) WITHOUT ROWID;
CREATE TABLE relationships (
  person TEXT PRIMARY KEY,
  conversations INTEGER NOT NULL,
  talking INTEGER NOT NULL,
  last_day REAL NOT NULL,
  regard INTEGER NOT NULL,
  mood TEXT NOT NULL,
  work TEXT NOT NULL,
  grievances INTEGER NOT NULL,
  forgiven INTEGER NOT NULL,
  asked_before TEXT NOT NULL,
  exposed INTEGER NOT NULL,
  eased_day REAL,
  days_met INTEGER NOT NULL,
  exchanges INTEGER NOT NULL,
  works_finished INTEGER NOT NULL,
  standing TEXT NOT NULL,
  called TEXT,
  called_how TEXT,
  called_other TEXT
) WITHOUT ROWID;
CREATE TABLE events (
  id TEXT PRIMARY KEY,
  seq INTEGER NOT NULL UNIQUE,
  kind TEXT NOT NULL,
  day REAL NOT NULL,
  place TEXT,
  place_name TEXT,
  node INTEGER,
  about_node INTEGER,
  person TEXT,
  person_name TEXT,
  quest_id TEXT,
  verb TEXT,
  purpose TEXT,
  difficulty INTEGER,
  was TEXT,
  revealed TEXT,
  player INTEGER NOT NULL,
  state TEXT NOT NULL,
  state_day REAL NOT NULL,
  actor TEXT,
  player_name TEXT,
  against TEXT,
  against_name TEXT,
  scope TEXT
) WITHOUT ROWID;
CREATE INDEX events_node ON events (node);
CREATE INDEX events_about ON events (about_node);
CREATE INDEX events_quest ON events (quest_id);
CREATE INDEX events_person ON events (person);
CREATE TABLE event_states (
  event TEXT NOT NULL,
  seq INTEGER NOT NULL,
  state TEXT NOT NULL,
  day REAL NOT NULL,
  actor TEXT,
  PRIMARY KEY (event, seq)
) WITHOUT ROWID;
CREATE TABLE facts (
  person TEXT NOT NULL,
  event TEXT NOT NULL,
  state TEXT NOT NULL,
  day REAL NOT NULL,
  how TEXT NOT NULL,
  PRIMARY KEY (person, event)
) WITHOUT ROWID;
CREATE INDEX facts_event ON facts (event);
CREATE TABLE accounts (
  event TEXT PRIMARY KEY,
  quest_id TEXT NOT NULL,
  day REAL NOT NULL,
  came TEXT,
  came_as TEXT,
  came_name TEXT,
  many TEXT,
  cost TEXT,
  part TEXT
) WITHOUT ROWID;
CREATE VIEW news AS
  SELECT f.person, f.event, e.kind, f.state, f.day AS learned, f.how, e.place, e.place_name, e.node,
         e.person AS about, e.player
  FROM facts AS f JOIN events AS e ON e.id = f.event;
CREATE TABLE exchanges (
  id INTEGER PRIMARY KEY,
  person TEXT NOT NULL,
  conversation INTEGER NOT NULL,
  turn INTEGER NOT NULL,
  day REAL NOT NULL,
  act TEXT NOT NULL,
  target TEXT NOT NULL,
  about TEXT NOT NULL,
  outcome TEXT NOT NULL,
  heard_from TEXT,
  of_exchange INTEGER,
  joined INTEGER NOT NULL,
  line TEXT,
  reply TEXT,
  asked TEXT
);
CREATE INDEX exchanges_person ON exchanges (person, id);
CREATE INDEX exchanges_of ON exchanges (of_exchange);
CREATE TABLE exchange_facts (
  exchange INTEGER NOT NULL,
  seq INTEGER NOT NULL,
  kind TEXT NOT NULL,
  subject TEXT NOT NULL,
  about TEXT NOT NULL,
  PRIMARY KEY (exchange, seq)
) WITHOUT ROWID;
CREATE INDEX exchange_facts_subject ON exchange_facts (subject);
CREATE TABLE memories (
  person TEXT NOT NULL,
  memory TEXT NOT NULL,
  subject TEXT NOT NULL,
  act TEXT NOT NULL,
  words TEXT NOT NULL,
  about TEXT NOT NULL,
  times INTEGER NOT NULL,
  first_day REAL NOT NULL,
  last_day REAL NOT NULL,
  source TEXT NOT NULL,
  seq INTEGER NOT NULL,
  PRIMARY KEY (person, memory, subject, act, words)
) WITHOUT ROWID;
CREATE INDEX memories_seq ON memories (person, seq);
CREATE VIEW active_short AS
  SELECT person, count(*) AS exchanges, min(day) AS since_day, max(day) AS last_day
  FROM exchanges GROUP BY person;
PRAGMA user_version = 5;
)sql";

[[noreturn]] void failed(sqlite3* db, const std::string& what) {
    throw std::runtime_error("brains: " + what + ": " + (db ? sqlite3_errmsg(db) : "no database"));
}

void exec(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) failed(db, std::string("running ") + sql);
}

// An in-memory database, and the settings every connection to it keeps: no file attached to it,
// ever, and a schema nothing but the director's own statements change.
sqlite3* opened() {
    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY, nullptr);
    if (rc != SQLITE_OK) {
        const std::string why = db ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close_v2(db);
        throw std::runtime_error("brains: opening the database: " + why);
    }
    sqlite3_limit(db, SQLITE_LIMIT_ATTACHED, 0);
    sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    return db;
}

// Reading, and nothing else: a game's query may select, read, call a function, recurse.
int reading_only(void*, int action, const char*, const char*, const char*, const char*) {
    switch (action) {
        case SQLITE_SELECT:
        case SQLITE_READ:
        case SQLITE_FUNCTION:
        case SQLITE_RECURSIVE:
            return SQLITE_OK;
        default:
            return SQLITE_DENY;
    }
}

int counted(void* left) {
    int& steps = *static_cast<int*>(left);
    return --steps <= 0 ? 1 : 0;
}

// The authorizer and the step count for one game query, taken off again however it ends.
class Guarded {
public:
    Guarded(sqlite3* db, int* steps) : db_(db) {
        sqlite3_set_authorizer(db_, reading_only, nullptr);
        sqlite3_progress_handler(db_, 1000, counted, steps);
    }
    ~Guarded() {
        sqlite3_set_authorizer(db_, nullptr, nullptr);
        sqlite3_progress_handler(db_, 0, nullptr, nullptr);
    }
    Guarded(const Guarded&) = delete;
    Guarded& operator=(const Guarded&) = delete;

private:
    sqlite3* db_;
};

class Finalized {
public:
    explicit Finalized(sqlite3_stmt* statement) : statement_(statement) {}
    ~Finalized() { sqlite3_finalize(statement_); }
    Finalized(const Finalized&) = delete;
    Finalized& operator=(const Finalized&) = delete;

private:
    sqlite3_stmt* statement_;
};

}  // namespace

Brains::Handle::~Handle() {
    for (auto& [sql, statement] : prepared) sqlite3_finalize(statement);
    sqlite3_close_v2(db);
}

Brains::Brains() : handle_(std::make_unique<Handle>()) {
    handle_->db = opened();
    exec(handle_->db, kSchema);
}

Brains::~Brains() = default;

Brains::Brains(const Brains& other) : handle_(std::make_unique<Handle>()) {
    handle_->db = opened();
    sqlite3_backup* backup = sqlite3_backup_init(handle_->db, "main", other.handle_->db, "main");
    if (backup == nullptr) failed(handle_->db, "copying the database");
    const int rc = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    if (rc != SQLITE_DONE) failed(handle_->db, "copying the database");
}

Brains& Brains::operator=(const Brains& other) {
    if (this != &other) {
        Brains copy(other);
        std::swap(handle_, copy.handle_);
    }
    return *this;
}

Brains::Brains(Brains&& other) noexcept = default;
Brains& Brains::operator=(Brains&& other) noexcept = default;

namespace brains {

Statement::Statement(Brains::Handle& handle, std::string_view sql) : db_(handle.db), statement_(nullptr) {
    auto found = handle.prepared.find(sql);
    if (found == handle.prepared.end()) {
        sqlite3_stmt* made = nullptr;
        if (sqlite3_prepare_v3(db_, sql.data(), static_cast<int>(sql.size()), SQLITE_PREPARE_PERSISTENT, &made, nullptr) != SQLITE_OK)
            failed(db_, "preparing " + std::string(sql));
        found = handle.prepared.emplace(std::string(sql), made).first;
    }
    statement_ = found->second;
}

Statement::~Statement() {
    sqlite3_reset(statement_);
    sqlite3_clear_bindings(statement_);
}

Statement& Statement::text(int index, std::string_view value) {
    if (sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        failed(db_, "binding text");
    return *this;
}

Statement& Statement::text_or_null(int index, std::string_view value) {
    if (value.empty()) {
        if (sqlite3_bind_null(statement_, index) != SQLITE_OK) failed(db_, "binding null");
        return *this;
    }
    return text(index, value);
}

Statement& Statement::integer(int index, std::int64_t value) {
    if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK) failed(db_, "binding an integer");
    return *this;
}

Statement& Statement::real(int index, double value) {
    if (sqlite3_bind_double(statement_, index, value) != SQLITE_OK) failed(db_, "binding a real");
    return *this;
}

Statement& Statement::maybe(int index, const std::optional<int>& value) {
    if (!value) {
        if (sqlite3_bind_null(statement_, index) != SQLITE_OK) failed(db_, "binding null");
        return *this;
    }
    return integer(index, *value);
}

bool Statement::step() {
    const int rc = sqlite3_step(statement_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    failed(db_, std::string("running ") + sqlite3_sql(statement_));
}

void Statement::run() {
    while (step()) {
    }
}

std::int64_t Statement::integer(int column) const { return sqlite3_column_int64(statement_, column); }

double Statement::real(int column) const { return sqlite3_column_double(statement_, column); }

std::string Statement::text(int column) const {
    const unsigned char* value = sqlite3_column_text(statement_, column);
    const int bytes = sqlite3_column_bytes(statement_, column);
    return value ? std::string(reinterpret_cast<const char*>(value), static_cast<std::size_t>(bytes)) : std::string();
}

std::optional<int> Statement::maybe(int column) const {
    if (null(column)) return std::nullopt;
    return sqlite3_column_int(statement_, column);
}

bool Statement::null(int column) const { return sqlite3_column_type(statement_, column) == SQLITE_NULL; }

Transaction::Transaction(Brains::Handle& handle) : handle_(handle) { exec(handle_.db, "SAVEPOINT brains"); }

Transaction::~Transaction() {
    if (done_) return;
    sqlite3_exec(handle_.db, "ROLLBACK TO brains; RELEASE brains", nullptr, nullptr, nullptr);
}

void Transaction::commit() {
    exec(handle_.db, "RELEASE brains");
    done_ = true;
}

QueryResult read_only(Brains::Handle& handle, std::string_view sql) {
    sqlite3* db = handle.db;
    int steps = kQueryStepsInThousands;
    const Guarded guard(db, &steps);
    sqlite3_stmt* statement = nullptr;
    const char* rest = nullptr;
    if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &statement, &rest) != SQLITE_OK)
        throw Error(std::string("query: ") + sqlite3_errmsg(db));
    if (statement == nullptr) throw Error("query: there is no statement in it");
    const Finalized finalized(statement);
    // Anything after the first statement but blanks and comments is a second one.
    const char* end = sql.data() + sql.size();
    if (rest != nullptr && rest < end) {
        sqlite3_stmt* next = nullptr;
        const int rc = sqlite3_prepare_v2(db, rest, static_cast<int>(end - rest), &next, nullptr);
        sqlite3_finalize(next);
        if (rc != SQLITE_OK || next != nullptr) throw Error("query: one statement at a time");
    }
    // Nothing but reading got past the authorizer: it denies every action it does not name, so a
    // write, a pragma, a transaction or a file attached fails to prepare above.

    QueryResult out;
    const int columns = sqlite3_column_count(statement);
    for (int c = 0; c < columns; ++c) {
        const char* name = sqlite3_column_name(statement, c);
        out.columns.emplace_back(name ? name : "");
    }
    for (;;) {
        const int rc = sqlite3_step(statement);
        if (rc == SQLITE_DONE) break;
        if (rc == SQLITE_INTERRUPT) throw Error("query: it ran too long, and was stopped");
        if (rc != SQLITE_ROW) throw Error(std::string("query: ") + sqlite3_errmsg(db));
        if (out.rows.size() == kQueryRows)
            throw Error("query: it answers more than " + std::to_string(kQueryRows) + " rows; ask for fewer (LIMIT)");
        std::vector<Cell>& row = out.rows.emplace_back();
        row.reserve(static_cast<std::size_t>(columns));
        for (int c = 0; c < columns; ++c) {
            switch (sqlite3_column_type(statement, c)) {
                case SQLITE_INTEGER:
                    row.emplace_back(static_cast<std::int64_t>(sqlite3_column_int64(statement, c)));
                    break;
                case SQLITE_FLOAT:
                    row.emplace_back(sqlite3_column_double(statement, c));
                    break;
                case SQLITE_NULL:
                    row.emplace_back(std::monostate{});
                    break;
                default: {  // text, or a blob read as its bytes
                    const auto* bytes = static_cast<const char*>(sqlite3_column_blob(statement, c));
                    const int size = sqlite3_column_bytes(statement, c);
                    row.emplace_back(bytes ? std::string(bytes, static_cast<std::size_t>(size)) : std::string());
                    break;
                }
            }
        }
    }
    return out;
}

}  // namespace brains
}  // namespace orchestrator
