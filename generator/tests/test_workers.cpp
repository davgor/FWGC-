// The thread count is not part of the world.
//
// The owner's ruling: a world generated on any number of threads is BYTE FOR BYTE the world
// generated on one, and that equality is proven before anything runs in parallel. So the
// tests here compare the WHOLE compact document -- the bytes `fwg generate` writes -- across
// worker counts, not a field or a block, and each one first proves it is looking at a world
// where an ordering defect could show at all: plans in all three planners, and a shape worn
// by two cities, so the repetition tally the cities share actually decides something.
//
// A failure is held to the same standard as a world: the call that throws on N threads throws
// what the serial call throws, of the same type, for the same site.
#include "check.hpp"

#include <algorithm>
#include <atomic>
#include <cfenv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <typeinfo>
#include <vector>

#ifdef _MSC_VER
#include <float.h>  // _controlfp_s: the denormal control an engine sets
// This file changes the floating-point environment at run time and must not have arithmetic
// folded or moved across those changes.
#pragma fenv_access(on)
#endif

#include "fwg/castle_plan.hpp"
#include "fwg/city_plan.hpp"
#include "fwg/complete_stage.hpp"
#include "fwg/grid.hpp"
#include "fwg/request_error.hpp"
#include "fwg/serialize.hpp"
#include "fwg/settlement_api.hpp"
#include "fwg/sha256.hpp"
#include "fwg/workers.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

// Sets the process-wide worker count for one scope and puts back the count it found, so a
// test that changes it cannot change the count every later test runs at.
class ScopedWorkers {
public:
    explicit ScopedWorkers(unsigned count) : before_(worker_threads()) { set_worker_threads(count); }
    ~ScopedWorkers() { set_worker_threads(before_); }
    ScopedWorkers(const ScopedWorkers&) = delete;
    ScopedWorkers& operator=(const ScopedWorkers&) = delete;

private:
    unsigned before_;
};

unsigned every_hardware_thread() {
    const ScopedWorkers all(0);
    return worker_threads();
}

// The counts a parallel run is compared at: two and three, where a scheduling defect is most
// likely to hand two neighbours to two threads, and every thread the machine has.
std::vector<unsigned> parallel_counts() {
    std::vector<unsigned> counts{2, 3};
    const unsigned most = every_hardware_thread();
    if (std::find(counts.begin(), counts.end(), most) == counts.end() && most > 1) counts.push_back(most);
    return counts;
}

// Serial first: the loop's own behaviour is held to the same contract at one worker.
std::vector<unsigned> all_counts() {
    std::vector<unsigned> counts = parallel_counts();
    counts.insert(counts.begin(), 1u);
    return counts;
}

// Puts the calling thread's floating-point environment back, whatever a test did to it.
class RestoreFloatEnvironment {
public:
    RestoreFloatEnvironment() { std::fegetenv(&saved_); }
    ~RestoreFloatEnvironment() { std::fesetenv(&saved_); }
    RestoreFloatEnvironment(const RestoreFloatEnvironment&) = delete;
    RestoreFloatEnvironment& operator=(const RestoreFloatEnvironment&) = delete;

private:
    std::fenv_t saved_{};
};

// Through volatiles, so the compiler cannot fold either quotient at build time in the default
// rounding mode.
double one_third() {
    volatile double one = 1.0;
    volatile double three = 3.0;
    return one / three;
}

double tiny_quotient() {
    volatile double tiny = 1e-300;
    volatile double huge = 1e15;
    return tiny / huge;
}

// A double's bits. Compared as bits, not as doubles: a caller flushing denormals also treats
// denormal INPUTS as zero, so `denormal == 0.0` is true on that thread and a comparison there
// cannot see whether a worker flushed.
std::uint64_t bits_of(double value) {
    std::uint64_t out = 0;
    std::memcpy(&out, &value, sizeof out);
    return out;
}

// `compute()` once on every thread of a pool of `workers`, the calling thread included: one
// task per thread, each waiting until all of them are running before it computes -- which can
// only finish if the pool really runs that many threads at once, and leaves no thread's value
// to chance. A queue of trivial tasks is usually drained by whichever thread starts first.
template <typename Fn>
std::vector<double> on_every_thread(unsigned workers, Fn&& compute) {
    std::atomic<unsigned> running{0};
    std::atomic<bool> gave_up{false};
    std::vector<double> values(workers, 0.0);
    std::vector<std::thread::id> ran_on(workers);
    for_each_index(workers, [&](std::size_t i) {
        ran_on[i] = std::this_thread::get_id();
        ++running;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (running.load() < workers && !gave_up.load()) {
            if (std::chrono::steady_clock::now() > deadline) gave_up = true;
            std::this_thread::yield();
        }
        values[i] = compute();
    });
    const std::string label = std::to_string(workers) + " workers";
    check::is_true(!gave_up.load(), "all " + label + " running at once");
    const std::set<std::thread::id> threads(ran_on.begin(), ran_on.end());
    check::equal(threads.size(), static_cast<std::size_t>(workers), "distinct threads among " + label);
    return values;
}

World generated(int size, std::uint32_t seed, unsigned workers) {
    const ScopedWorkers scoped(workers);
    Request request;
    request.seed = seed;
    request.size = size;
    request.phase = kHighestImplementedStage;
    return generate(resolve(request));
}

// The compact document `fwg generate` writes, without timings, as a SHA-256.
std::string document_sha256(const World& world) {
    std::ostringstream out;
    write_world_json(out, world, /*pretty=*/false, /*include_timings=*/false);
    return sha256_hex(std::move(out).str());
}

std::string where(int size, std::uint32_t seed) {
    return "size " + std::to_string(size) + ", seed " + std::to_string(seed);
}

// A world in which parallel planning could go wrong visibly: all three planners drew plans,
// several of each, and at least one shape is worn by two cities -- so the second of them was
// drawn against a tally that counted the first. Without that, a defect in how the tally is
// shared would change nothing and every comparison below would pass for the wrong reason.
void require_plans_that_can_disagree(const World& world, const std::string& label) {
    check::equal(world.completed_stage, kHighestImplementedStage, label + ": the stage reached");
    check::is_true(world.has_plans && world.has_rural_plans, label + ": city and rural plans");
    const std::size_t cities = world.city_plans.cities.size();
    const std::size_t hamlets = world.hamlet_plans.hamlets.size();
    const std::size_t castles = world.castle_plans.castles.size();
    check::is_true(cities >= 8, label + ": enough cities to share a pool, got " + std::to_string(cities));
    check::is_true(hamlets >= 8, label + ": enough hamlets to share a pool, got " + std::to_string(hamlets));
    check::is_true(castles >= 8, label + ": enough castles to share a pool, got " + std::to_string(castles));
    std::map<std::string_view, int> worn;
    for (const CityPlan& plan : world.city_plans.cities) {
        if (!plan.shape.shape_id.empty()) ++worn[plan.shape.shape_id];
    }
    int most = 0;
    for (const auto& entry : worn) most = std::max(most, entry.second);
    check::is_true(most >= 2, label + ": a shape worn by two cities, so the repetition tally decides something");
}

// Every world in `seeds` at `size`, serially and at each parallel count, compared whole.
void hold_to_the_serial_document(int size, std::initializer_list<std::uint32_t> seeds) {
    const std::vector<unsigned> counts = parallel_counts();
    for (const std::uint32_t seed : seeds) {
        const World serial = generated(size, seed, 1);
        require_plans_that_can_disagree(serial, where(size, seed));
        const std::string expected = document_sha256(serial);
        for (const unsigned workers : counts) {
            check::equal(document_sha256(generated(size, seed, workers)), expected,
                         where(size, seed) + " on " + std::to_string(workers) +
                             " workers: the SHA-256 of the whole compact document");
        }
    }
}

// --- the planners on their own ----------------------------------------------------------

struct Ground {
    World world;
    std::shared_ptr<const SphereGrid> grid;
};

Ground ground_at(int phase, int size, std::uint32_t seed = 42) {
    Request request;
    request.seed = seed;
    request.size = size;
    request.phase = phase;
    Ground out;
    out.world = generate(resolve(request));
    // effective, not config: every reach in a plan is metres.
    out.grid = sphere_grid(out.world.config.size, out.world.effective.globe_radius);
    return out;
}

// `count` medium cities on dry, gentle land away from the poles, keyed "city-0" upward --
// so from ten sites on the key order is not the numeric one.
std::vector<CitySite> sites_on_land(const Ground& ground, int count) {
    const int n = ground.world.config.size;
    const Grid& water = ground.world.layers.at("water_type");
    const Grid& slope = ground.world.layers.at("slope");
    std::vector<int> land;
    for (std::size_t i = 0; i < ground.grid->node_count(); ++i) {
        const int x = ground.grid->point_x[i];
        const int z = ground.grid->point_z[i];
        if (z < 4 || z > n - 5) continue;
        if (water.at(x, z) != 0.0 || slope.at(x, z) > 12.0) continue;
        land.push_back(static_cast<int>(i));
    }
    const std::size_t step = std::max<std::size_t>(1, land.size() / static_cast<std::size_t>(std::max(1, count)));
    std::vector<CitySite> sites;
    for (int k = 0; k < count && static_cast<std::size_t>(k) * step < land.size(); ++k) {
        const int node = land[static_cast<std::size_t>(k) * step];
        CitySite site;
        site.uid = "city-" + std::to_string(k);
        site.id = k;
        site.name = "City " + std::to_string(k);
        site.x = ground.grid->point_x[static_cast<std::size_t>(node)];
        site.z = ground.grid->point_z[static_cast<std::size_t>(node)];
        site.city_class = CityClass::Medium;
        site.population_profile = "human_heartland";
        site.freshwater_distance_m = 120.0;
        site.resource_potential = 0.6;
        sites.push_back(std::move(site));
    }
    return sites;
}

CitySite& site_keyed(std::vector<CitySite>& sites, const std::string& uid) {
    for (CitySite& site : sites) {
        if (site.uid == uid) return site;
    }
    check::fail("no site keyed " + uid);
}

// The city plans as the document writes them, hashed: attached to the world they were drawn
// on, so every field the writer reads -- and nothing chosen by this test -- is compared.
std::string city_plans_sha256(const World& world, CityPlans plans) {
    World holder = world;
    holder.city_plans = std::move(plans);
    holder.has_plans = true;
    holder.has_rural_plans = false;
    return document_sha256(holder);
}

// What fill_cities did before it had a pool, written out here as the reference its pooled
// version answers to: every city planned whole, in key order, each drawing its shape against
// the tally the cities before it left -- and a city the player laid out counting for nothing.
CityPlans planned_one_at_a_time(const World& world, const SphereGrid& grid, std::vector<CitySite> sites) {
    std::stable_sort(sites.begin(), sites.end(),
                     [](const CitySite& a, const CitySite& b) { return a.key() < b.key(); });
    CityPlans out = fill_cities(world, grid, {});
    std::map<std::string_view, int> counts;
    for (const CitySite& site : sites) {
        CityPlan plan = plan_city(world, grid, site, sites, counts);
        if (!plan.shape.shape_id.empty()) ++counts[plan.shape.shape_id];
        out.cities.push_back(std::move(plan));
    }
    return out;
}

// What a call threw, as far as a caller can tell two failures apart.
struct Thrown {
    bool threw = false;
    std::string type;
    std::string message;
};

template <typename Fn>
Thrown thrown_by(Fn&& fn) {
    try {
        fn();
    } catch (const std::exception& error) {
        return {true, typeid(error).name(), error.what()};
    }
    return {};
}

// The serial call's failure, then the same failure at every parallel count. `names_the_site`
// is the part of the message only the site serial planning stopped at would produce.
template <typename Fn>
void fails_as_serial_does(Fn&& call, const std::string& names_the_site, const std::string& label) {
    Thrown serial;
    {
        const ScopedWorkers one(1);
        serial = thrown_by(call);
    }
    check::is_true(serial.threw, label + ": the serial call to fail");
    check::is_true(serial.message.find(names_the_site) != std::string::npos,
                   label + ": the serial call to fail at the site expected, naming '" + names_the_site +
                       "', and it said: " + serial.message);
    for (const unsigned workers : parallel_counts()) {
        const ScopedWorkers scoped(workers);
        const Thrown pooled = thrown_by(call);
        const std::string at = label + " on " + std::to_string(workers) + " workers";
        check::is_true(pooled.threw, at + ": to fail as the serial call does");
        check::equal(pooled.type, serial.type, at + ": the exception's type");
        check::equal(pooled.message, serial.message, at + ": the exception's message");
    }
}

std::vector<HamletPin> pins_of(const World& world) {
    std::vector<HamletPin> pins;
    for (const RuralSite& h : world.humans.hamlets) {
        HamletPin pin;
        pin.id = h.id;
        pin.uid = h.uid;
        pin.x = h.x;
        pin.z = h.z;
        pin.node = h.node;
        pin.core_id = h.core_id;
        pin.population_profile = h.population_profile;
        pin.role = h.role;
        pin.access_nodes = h.access_nodes;
        pins.push_back(std::move(pin));
    }
    return pins;
}

}  // namespace

// --- the setting -----------------------------------------------------------------------------

TEST(the_worker_count_is_a_process_setting_and_never_zero) {
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    {
        const ScopedWorkers all(0);
        check::equal(worker_threads(), hardware, "0 to mean every hardware thread");
    }
    {
        const ScopedWorkers one(1);
        check::equal(worker_threads(), 1u, "1 to mean serial");
    }
    {
        const ScopedWorkers three(3);
        check::equal(worker_threads(), 3u, "a count to be kept as given");
    }
}

// --- the loop ---------------------------------------------------------------------------------

TEST(every_index_runs_once_and_its_result_lands_in_its_own_slot) {
    for (const unsigned workers : all_counts()) {
        const ScopedWorkers scoped(workers);
        std::vector<int> runs(5000, 0);
        std::vector<std::size_t> squares(runs.size(), 0);
        for_each_index(runs.size(), [&](std::size_t i) {
            ++runs[i];
            squares[i] = i * i;
        });
        for (std::size_t i = 0; i < runs.size(); ++i) {
            if (runs[i] != 1 || squares[i] != i * i) {
                check::fail("on " + std::to_string(workers) + " workers index " + std::to_string(i) + " ran " +
                            std::to_string(runs[i]) + " times");
            }
        }
        bool ran = false;
        for_each_index(0, [&](std::size_t) { ran = true; });
        check::is_true(!ran, "an empty range to run nothing");
    }
}

// A pool that quietly ran everything on the calling thread would pass every comparison in this
// file for the wrong reason. N tasks that each wait until all N are running can only finish if
// N threads run them at once.
TEST(the_pool_runs_on_as_many_threads_as_it_is_given) {
    for (const unsigned workers : parallel_counts()) {
        const ScopedWorkers scoped(workers);
        const std::vector<double> ran = on_every_thread(workers, [] { return 1.0; });
        check::equal(ran.size(), static_cast<std::size_t>(workers), "one task per thread");
    }
}

// The lowest index that threw is the one reported, however the threads raced -- and a pool can
// get that wrong two ways, so there are two races. Serial order says index 5 both times.
TEST(a_failure_is_the_lowest_index_that_threw_not_the_first_or_last_to_throw) {
    const std::string out_of_range_type(typeid(std::out_of_range).name());
    for (const unsigned workers : all_counts()) {
        const ScopedWorkers scoped(workers);
        const std::string at = "on " + std::to_string(workers) + " workers";

        // The low index fails LAST: a pool keeping the first failure in time reports index 6.
        std::vector<int> ran(64, 0);
        const Thrown late = thrown_by([&] {
            for_each_index(ran.size(), [&](std::size_t i) {
                ran[i] = 1;
                if (i == 5) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                    throw std::out_of_range("index 5");
                }
                if (i == 6 || i == 40) throw std::invalid_argument("index " + std::to_string(i));
            });
        });
        check::is_true(late.threw, at + ", low index failing last: the failure to reach the caller");
        check::equal(late.message, std::string("index 5"), at + ", low index failing last: the lowest index");
        check::equal(late.type, out_of_range_type, at + ", low index failing last: its type, unchanged");
        for (std::size_t i = 0; i < 5; ++i) {
            check::equal(ran[i], 1, at + ": index " + std::to_string(i) + ", below the failure, to have run");
        }

        // The low index fails FIRST, and only once index 6 is already running; 6 fails after it.
        // A pool keeping the last failure reports index 6. On one worker nothing runs beside 5.
        std::atomic<bool> six_started{false};
        const Thrown early = thrown_by([&] {
            for_each_index(64, [&](std::size_t i) {
                if (i == 6) {
                    six_started = true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                    throw std::invalid_argument("index 6");
                }
                if (i == 5) {
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    while (workers > 1 && !six_started.load() && std::chrono::steady_clock::now() < deadline) {
                        std::this_thread::yield();
                    }
                    throw std::out_of_range("index 5");
                }
            });
        });
        check::is_true(workers == 1 || six_started.load(), at + ": index 6 running when index 5 failed");
        check::is_true(early.threw, at + ", low index failing first: the failure to reach the caller");
        check::equal(early.message, std::string("index 5"), at + ", low index failing first: the lowest index");
        check::equal(early.type, out_of_range_type, at + ", low index failing first: its type, unchanged");
    }
}

// A thread starts in the DEFAULT floating-point environment, not its creator's. Rounding
// upward in the caller, every task must round upward too, or a host that sets a rounding mode
// would get one world from the pool and another from the loop. One task on every thread: a
// queue of trivial tasks is often drained by one thread before the rest have started, and a
// test that let it would pass with the environment never handed on.
TEST(every_task_runs_in_the_callers_rounding_mode) {
    const RestoreFloatEnvironment restore;
    const double nearest = one_third();
    check::is_true(std::fesetround(FE_UPWARD) == 0, "the caller's rounding mode to be settable");
    const double upward = one_third();
    check::is_true(upward != nearest, "1/3 to round differently upward, or this proves nothing");
    for (const unsigned workers : all_counts()) {
        const ScopedWorkers scoped(workers);
        for (const double value : on_every_thread(workers, one_third)) {
            check::is_true(bits_of(value) == bits_of(upward), "the task on every one of " + std::to_string(workers) +
                                                                  " threads to round upward");
        }
    }
}

#ifdef _MSC_VER
// The realistic case: a game engine that flushes denormals to zero on the thread it calls in
// from. MSVC's fenv_t carries the flush bits, and the pool must hand them on.
TEST(every_task_flushes_denormals_when_the_caller_does) {
    const RestoreFloatEnvironment restore;
    check::is_true(bits_of(tiny_quotient()) != 0u, "a denormal quotient to survive by default");
    unsigned int control = 0;
    check::equal(_controlfp_s(&control, _DN_FLUSH, _MCW_DN), 0, "the caller's denormal control to be settable");
    check::is_true(bits_of(tiny_quotient()) == 0u, "the caller to flush it to +0 once asked to");
    for (const unsigned workers : all_counts()) {
        const ScopedWorkers scoped(workers);
        for (const double value : on_every_thread(workers, tiny_quotient)) {
            check::is_true(bits_of(value) == 0u, "the task on every one of " + std::to_string(workers) +
                                                     " threads to flush it to +0 too");
        }
    }
}
#endif

// --- whole worlds ---------------------------------------------------------------------------

TEST(a_world_is_byte_identical_at_every_worker_count_at_size_33) {
    hold_to_the_serial_document(33, {42, 7});
}

TEST(a_world_is_byte_identical_at_every_worker_count_at_size_65) {
    hold_to_the_serial_document(65, {42});
}

// redraw_plans is what a summons and a departure call, over a world that already has plans
// and here one city the player laid out -- which gets no shape and must not count in the
// tally the cities after it draw against.
TEST(a_redraw_over_a_player_city_is_byte_identical_at_every_worker_count) {
    const World genesis = generated(33, 42, 1);
    const auto held = sphere_grid(genesis.config.size, genesis.effective.globe_radius);
    FoundSettlementRequest request;
    request.civilization_id = genesis.settlements.sites.at(0).population_profile;
    request.name = "Playerholm";
    World founded;
    bool placed = false;
    for (std::size_t node = 0; node < held->node_count() && !placed; ++node) {
        request.node = static_cast<int>(node);
        try {
            founded = found_settlement(genesis, request);
            placed = true;
        } catch (const RequestError&) {
        }
    }
    check::is_true(placed, "some node of the world to accept a player city");

    World serial = founded;
    {
        const ScopedWorkers one(1);
        redraw_plans(serial);
    }
    const std::vector<CityPlan>& cities = serial.city_plans.cities;
    const auto authored =
        std::find_if(cities.begin(), cities.end(), [](const CityPlan& p) { return p.authored_by_player; });
    check::is_true(authored != cities.end(), "the redraw to plan the player's city among the others");
    // Cities after it in key order, or a defect in how the pool treats it could change nothing.
    check::is_true(authored + 1 != cities.end(), "cities after the player's in key order");
    require_plans_that_can_disagree(serial, "the redrawn world");
    const std::string expected = document_sha256(serial);

    for (const unsigned workers : parallel_counts()) {
        World pooled = founded;
        {
            const ScopedWorkers scoped(workers);
            redraw_plans(pooled);
        }
        check::equal(document_sha256(pooled), expected,
                     "the redraw on " + std::to_string(workers) + " workers: the whole document's SHA-256");
    }
}

// The pooled city planner against the loop it replaced, written out in this file: the
// comparison above holds the pool to the serial path, and this holds the serial path -- and
// the pool -- to the tally rule itself, including the player's city that draws a shape and
// then does not count it.
TEST(every_city_draws_its_shape_against_the_tally_the_cities_before_it_left) {
    const Ground ground = ground_at(9, 65);
    std::vector<CitySite> sites = sites_on_land(ground, 14);
    check::is_true(sites.size() >= 11, "enough cities for key and numeric order to differ");
    // Third in key order (city-0, city-1, city-10, ...), so every city after it would feel a
    // count it wrongly added.
    site_keyed(sites, "city-10").founded_by_player = true;

    const CityPlans reference = planned_one_at_a_time(ground.world, *ground.grid, sites);
    // The player's city must have had shapes to draw from -- its candidates are kept though its
    // shape is not -- and cities must follow it, or counting it could change nothing and the
    // rule would go untested.
    const auto player = std::find_if(reference.cities.begin(), reference.cities.end(),
                                     [](const CityPlan& p) { return p.authored_by_player; });
    check::is_true(player != reference.cities.end(), "the player's city to be planned");
    check::is_true(!player->shape.candidates.empty(), "the player's city to have had shapes to draw from");
    check::is_true(player + 1 != reference.cities.end(), "cities after the player's in key order");
    std::map<std::string_view, int> worn;
    for (const CityPlan& plan : reference.cities) {
        if (!plan.shape.shape_id.empty()) ++worn[plan.shape.shape_id];
    }
    int most = 0;
    for (const auto& entry : worn) most = std::max(most, entry.second);
    check::is_true(most >= 2, "a shape worn by two cities, so the tally decides something");
    const std::string expected = city_plans_sha256(ground.world, reference);

    for (const unsigned workers : all_counts()) {
        const ScopedWorkers scoped(workers);
        check::equal(city_plans_sha256(ground.world, fill_cities(ground.world, *ground.grid, sites)), expected,
                     "fill_cities on " + std::to_string(workers) + " workers against the one-at-a-time loop");
    }
}

// --- failures ---------------------------------------------------------------------------------

// Serial planning stops at the first city in key order that fails, in whichever part of its
// plan it fails. A city whose shape draw is refused (a threat outside the catalogue's declared
// 0..1, which the planner also reads as its defence priority and reports under that name)
// comes BEFORE a city whose ground cannot even be surveyed (a people with no presets), so the
// pool -- which surveys every city before it draws any shape -- must still report the first.
TEST(a_city_failure_is_the_one_serial_planning_reports) {
    const Ground ground = ground_at(9, 33);
    const std::vector<CitySite> base = sites_on_land(ground, 8);
    check::is_true(base.size() >= 8, "eight cities to plan");

    std::vector<CitySite> draw_first = base;
    site_keyed(draw_first, "city-2").regional_threat = 1.5;
    site_keyed(draw_first, "city-5").population_profile = "people-of-city-5";
    fails_as_serial_does([&] { (void)fill_cities(ground.world, *ground.grid, draw_first); }, "defense_priority: 1.5",
                         "a refused shape draw before an unsurveyable city");

    std::vector<CitySite> survey_first = base;
    site_keyed(survey_first, "city-2").population_profile = "people-of-city-2";
    site_keyed(survey_first, "city-5").regional_threat = 1.75;
    fails_as_serial_does([&] { (void)fill_cities(ground.world, *ground.grid, survey_first); }, "people-of-city-2",
                         "an unsurveyable city before a refused shape draw");

    std::vector<CitySite> both_drawn = base;
    site_keyed(both_drawn, "city-6").regional_threat = 1.25;
    site_keyed(both_drawn, "city-3").regional_threat = 1.75;
    fails_as_serial_does([&] { (void)fill_cities(ground.world, *ground.grid, both_drawn); }, "defense_priority: 1.75",
                         "two refused shape draws");
}

// Hamlets are planned in id order, which is lexicographic: hamlet-10 before hamlet-9.
TEST(a_hamlet_failure_is_the_one_serial_planning_reports) {
    const Ground ground = ground_at(12, 33);
    std::vector<HamletPin> pins = pins_of(ground.world);
    check::is_true(pins.size() >= 12, "a dozen hamlets to plan, got " + std::to_string(pins.size()));
    for (HamletPin& pin : pins) {
        if (pin.id == "hamlet-9" || pin.id == "hamlet-10") pin.uid.clear();
    }
    const std::vector<CitySite> no_cities;
    fails_as_serial_does([&] { (void)fill_hamlets(ground.world, *ground.grid, no_cities, pins); },
                         "hamlet hamlet-10 has", "two hamlets with no uid");
}

// A castle's only per-site refusal names no site -- every fortress without a uid is refused in
// the same words -- so this holds the pool to the serial type and message, and
// a_failure_is_the_lowest_index_that_threw_not_the_first_or_last_to_throw holds the loop
// fill_castles runs on to the lowest index.
TEST(a_castle_failure_is_the_one_serial_planning_reports) {
    const Ground ground = ground_at(12, 33);
    std::vector<RuralSite> fortresses = ground.world.humans.fortresses;
    check::is_true(fortresses.size() >= 8, "eight fortresses to plan, got " + std::to_string(fortresses.size()));
    fortresses[fortresses.size() / 2].uid.clear();
    CastleRequest request;
    request.world = &ground.world;
    request.grid = ground.grid.get();
    request.globe_radius_m = ground.world.effective.globe_radius;
    request.sites = ground.world.settlements.sites;
    request.fortresses = fortresses;
    fails_as_serial_does([&] { (void)fill_castles(request); }, "stable uid", "a fortress with no uid");
}
