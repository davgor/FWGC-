// Climate invariants.
//
// Rainfall and runoff here are RELATIVE proxies, so almost nothing about them has an
// absolute value worth pinning. What can be pinned is that they conserve, that they
// stay in their ranges, and that each one moves when the knob that should move it
// moves.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include "fwg/climate.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

Request request_at(int size) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = 6;  // the reference gates climate on phase >= 6
    return r;
}

World build(int size = 65) { return generate(resolve(request_at(size))); }

double mean_of(const World& w, const char* layer) {
    const Grid& g = w.layers.at(layer);
    double total = 0.0;
    int count = 0;
    for (int z = 0; z < g.size(); ++z) {
        for (int x = 0; x < g.size() - 1; ++x) {
            total += g.at(x, z);
            ++count;
        }
    }
    return total / count;
}

}  // namespace

TEST(climate_runs_only_from_phase_six) {
    const World five = generate(resolve(request_at(33)));  // phase 6 by default here
    check::is_true(five.has_climate, "phase 6 to have climate");

    Request early = request_at(33);
    early.phase = 5;
    const World world = generate(resolve(early));
    check::is_true(!world.has_climate, "phase 5 to have no climate");
    check::equal(world.layers.count("rainfall"), std::size_t{0}, "no rainfall layer at phase 5");
}

TEST(upwind_stencil_weights_are_a_partition_of_unity) {
    // If these do not sum to one the transport creates or destroys moisture out of
    // nowhere, and every field downstream is quietly wrong by a scale factor.
    const auto held = sphere_grid(33, 100000.0);
    for (const double bearing : {0.0, 1.0, 2.5, 4.7, 6.0}) {
        const auto stencils = upwind_stencils(*held, bearing);
        check::equal(stencils.size(), held->node_count(), "one stencil per node");
        for (std::size_t i = 0; i < stencils.size(); ++i) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                check::is_true(stencils[i].weight[k] >= 0.0, "no negative weight");
                check::is_true(stencils[i].node[k] >= 0 &&
                                   static_cast<std::size_t>(stencils[i].node[k]) < held->node_count(),
                               "stencil node in range");
                sum += stencils[i].weight[k];
            }
            check::close(sum, 1.0, 1e-12, "stencil " + std::to_string(i) + " to sum to one");
        }
    }
}

TEST(runoff_is_conserved_from_rainfall_to_outlets) {
    // Every cell's rainfall reaches exactly one outlet. This is the real check on the
    // leaf-to-root accumulation, and it fails loudly if the receiver graph has a cycle
    // or a node is never drained.
    for (const int size : {33, 129}) {
        const World world = build(size);
        const ClimateReport& c = world.climate;
        check::is_true(c.runoff_input_m2_equivalent > 0.0, "some rain to fall");
        check::close(c.runoff_outlets_m2_equivalent / c.runoff_input_m2_equivalent, 1.0, 1e-9,
                     "outlets to carry all the rain at size " + std::to_string(size));
    }
}

TEST(the_climate_fields_stay_in_their_ranges) {
    const World world = build();
    for (const double v : world.layers.at("climate_moisture").cells()) {
        check::between(v, 0.0, 1.0, "climate_moisture is a 0..1 wetness");
    }
    for (const double v : world.layers.at("air_moisture").cells()) {
        check::between(v, 0.0, 1.0, "air_moisture is a 0..1 saturation");
    }
    for (const double v : world.layers.at("rainfall").cells()) {
        check::is_true(v >= 0.0, "rainfall is non-negative");
    }
    for (const double v : world.layers.at("wind_uplift").cells()) {
        check::is_true(v >= 0.0, "uplift counts only rises, never descents");
    }
    for (const double v : world.layers.at("rain_river").cells()) {
        check::is_true(v == 0.0 || v == 1.0, "rain_river is a flag");
    }
}

TEST(the_transport_step_is_one_cell_of_arc) {
    const World world = build(65);
    const double expected = 2.0 * std::numbers::pi * world.effective.globe_radius / (world.config.size - 1);
    check::close(world.climate.transport_step_m, expected, 1e-6, "transport step");
}

TEST(more_passes_settle_the_field_further) {
    // The residual is the largest humidity change on the final pass. It is not a
    // convergence guarantee and the document says so, but it must at least fall as
    // the field settles -- if it does not, the transport is not a contraction and the
    // pass count means nothing.
    Request few = request_at(33);
    few.rain_passes = 4;
    Request many = request_at(33);
    many.rain_passes = 64;
    const double coarse = generate(resolve(few)).climate.residual;
    const double fine = generate(resolve(many)).climate.residual;
    check::is_true(fine < coarse, "more passes to leave a smaller residual (" + std::to_string(coarse) +
                                      " -> " + std::to_string(fine) + ")");
}

// --- the metrics move with their knobs -----------------------------------

TEST(a_hotter_world_is_a_drier_one) {
    // Not because less rain falls -- the same rain falls -- but because demand rises
    // with temperature, so the same rainfall supports less. That is the whole point
    // of putting temperature in the wetness term rather than reading rainfall raw.
    Request cold = request_at(65);
    cold.temperature_offset = -20.0;
    Request hot = request_at(65);
    hot.temperature_offset = 20.0;

    const World cool = generate(resolve(cold));
    const World warm = generate(resolve(hot));
    check::is_true(mean_of(warm, "climate_moisture") < mean_of(cool, "climate_moisture"),
                   "heat to dry the world out");
    // And the rainfall itself should be untouched: temperature does not move the air.
    check::close(mean_of(warm, "rainfall"), mean_of(cool, "rainfall"), 1e-12,
                 "temperature not to change how much rain falls");
}

TEST(rainfall_answers_to_wind_bearing) {
    Request east = request_at(65);
    east.wind_bearing = 90.0;
    Request west = request_at(65);
    west.wind_bearing = 270.0;
    const World a = generate(resolve(east));
    const World b = generate(resolve(west));

    const Grid& ra = a.layers.at("rainfall");
    const Grid& rb = b.layers.at("rainfall");
    int moved = 0;
    for (int z = 0; z < ra.size(); ++z) {
        for (int x = 0; x < ra.size(); ++x) {
            if (std::abs(ra.at(x, z) - rb.at(x, z)) > 1e-9) ++moved;
        }
    }
    check::is_true(moved > ra.size() * ra.size() / 10,
                   "reversing the wind to move the rain (" + std::to_string(moved) + " cells)");
}

TEST(rainfall_answers_to_rain_strength) {
    Request weak = request_at(65);
    weak.rain_strength = 0.05;
    Request strong = request_at(65);
    strong.rain_strength = 3.0;
    check::is_true(mean_of(generate(resolve(strong)), "rainfall") > mean_of(generate(resolve(weak)), "rainfall"),
                   "stronger orographic rainout to drop more rain");
}

TEST(moisture_answers_to_its_bias) {
    Request dry = request_at(33);
    dry.moisture_bias = -0.5;
    Request wet = request_at(33);
    wet.moisture_bias = 0.5;
    check::is_true(mean_of(generate(resolve(wet)), "climate_moisture") >
                       mean_of(generate(resolve(dry)), "climate_moisture"),
                   "the bias to move the wetness");
}

TEST(temperature_falls_from_equator_to_pole_and_with_height) {
    const double equator = surface_temperature({1.0, 0.0, 0.0}, 0.0, 0.0, 0.0);
    const double pole = surface_temperature({0.0, 1.0, 0.0}, 0.0, 0.0, 0.0);
    check::close(equator, 28.0, 1e-12, "equatorial sea level");
    check::close(pole, -17.0, 1e-12, "polar sea level");
    check::is_true(pole < equator, "poles to be colder than the equator");

    const double high = surface_temperature({1.0, 0.0, 0.0}, 2000.0, 0.0, 0.0);
    check::close(equator - high, 2000.0 * 0.0065, 1e-12, "a 6.5 C/km lapse rate");

    // Below the datum nothing warms: depth is not negative altitude.
    check::close(surface_temperature({1.0, 0.0, 0.0}, -3000.0, 0.0, 0.0), equator, 1e-12,
                 "ocean depth not to heat the air");
    check::close(surface_temperature({1.0, 0.0, 0.0}, 0.0, 0.0, 5.0), equator + 5.0, 1e-12, "the offset");
}

TEST(wetness_rises_with_rain_and_falls_with_heat) {
    check::is_true(climate_wetness(0.2, 10.0) > climate_wetness(0.05, 10.0), "more rain, more wetness");
    check::is_true(climate_wetness(0.1, 30.0) < climate_wetness(0.1, 10.0), "more heat, less wetness");
    check::close(climate_wetness(0.0, 10.0), 0.0, 1e-12, "no rain, no wetness");
    check::between(climate_wetness(1e6, 10.0), 0.99, 1.0, "wetness saturates at one");
    // Below 10 C demand stops falling, so two cold places compare equal.
    check::close(climate_wetness(0.1, -30.0), climate_wetness(0.1, 10.0), 1e-12, "demand floors at 10 C");
}
