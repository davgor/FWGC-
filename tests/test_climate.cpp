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

TEST(rain_falls_on_the_flank_the_wind_reaches_first) {
    // The stencil test above is algebra: the four bilinear weights sum to one for any
    // fx and fz at all, so it holds whichever cell the stencil samples. What it cannot
    // see is the one thing that can really be wrong -- the DIRECTION of the upwind
    // step. Nor can any correlation test: a model that samples downwind still rains on
    // mountains, it just rains on the wrong face, and "rain follows terrain" is true
    // either way. Separating the two takes a world where the windward flank is not in
    // doubt.
    //
    // So: one north-south ridge with flat sea either side, and a due-east wind. The
    // elevation depends on longitude alone, which is what makes this decidable -- one
    // step against an easterly wind is one step toward a smaller x at every latitude,
    // so the windward flank is the smaller-x one everywhere on the globe.
    const int n = 65;
    const int half = 8;         // columns from the crest down to the foot
    const double peak = 800.0;  // 100 m of rise per column: a real slope, under the rainout cap
    const auto held = sphere_grid(n, 6000000.0);
    const SphereGrid& grid = *held;
    const int crest = (n - 1) / 2;

    std::vector<double> surface(grid.node_count(), 0.0);
    std::vector<char> is_water(grid.node_count(), 1);
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        const int d = std::abs(grid.point_x[i] - crest);
        if (d >= half) continue;
        surface[i] = peak * (1.0 - static_cast<double>(d) / half);
        is_water[i] = 0;  // the ridge is land; the rest is the sea the air drinks from
    }

    // Bearing runs clockwise from north and the air is sampled one step AGAINST it, so
    // 90 degrees is a wind running west to east.
    const auto upwind = upwind_stencils(grid, std::numbers::pi / 2.0);
    const MoistureField air = transport_moisture(surface, is_water, upwind, 24, 1.0);

    // A step of arc is one column at the equator and 1/cos(latitude) columns away from
    // it. Inside 50 degrees that is at most 1.56 columns, short enough that no lee
    // cell's upwind sample can reach over the crest and find lower ground.
    double windward_uplift = 0.0, windward_rain = 0.0, lee_rain = 0.0, worst_lee_uplift = 0.0;
    int windward = 0, lee = 0;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        const double latitude = 90.0 - 180.0 * grid.point_z[i] / (n - 1);
        if (std::abs(latitude) > 50.0) continue;
        const int dx = grid.point_x[i] - crest;
        if (dx <= -half || dx >= half || dx == 0) continue;  // the two flanks, not the crest or the sea
        if (dx < 0) {
            windward_uplift += air.uplift[i];
            windward_rain += air.rain[i];
            ++windward;
        } else {
            lee_rain += air.rain[i];
            worst_lee_uplift = std::max(worst_lee_uplift, air.uplift[i]);
            ++lee;
        }
    }
    check::is_true(windward > 150 && lee > 150, "both flanks to be sampled (" + std::to_string(windward) +
                                                    " windward, " + std::to_string(lee) + " lee)");

    // The ridge climbs 100 m a column, so the air arriving at a windward cell has
    // climbed roughly that. Half of it is comfortably clear of nothing.
    const double windward_mean_uplift = windward_uplift / windward;
    check::is_true(windward_mean_uplift > 50.0,
                   "the windward flank to lift the arriving air (" + std::to_string(windward_mean_uplift) + " m)");

    // And the lee does not lift it at all: descending air is not wrung out, which is
    // the dry shadow the module header sells. This is the assertion a reversed upwind
    // step cannot survive -- it moves every metre of that uplift onto this flank.
    check::is_true(worst_lee_uplift <= 1e-9,
                   "no lee cell to lift the air (worst " + std::to_string(worst_lee_uplift) + " m)");

    const double windward_mean_rain = windward_rain / windward;
    const double lee_mean_rain = lee_rain / lee;
    check::is_true(windward_mean_rain > 100.0 * lee_mean_rain,
                   "the wind to drop its rain on the flank it reaches first (" +
                       std::to_string(windward_mean_rain) + " windward vs " + std::to_string(lee_mean_rain) + " lee)");
}

TEST(a_generated_world_rains_on_its_windward_slopes) {
    // The same property end to end, so the path from Request::wind_bearing through to
    // the rainfall layer is pinned as well as the stencil.
    //
    // Which way is upwind is worked out here from the water_surface layer -- settled at
    // phase 5, before the climate stage, and never revised by it -- rather than from
    // the stencil under test. If the test asked the stencil where the air came from, a
    // reversal would reverse both sides of the comparison and the test would pass on a
    // backwards world.
    for (const double bearing : {90.0, 270.0}) {
        Request request = request_at(65);
        request.wind_bearing = bearing;
        const World world = generate(resolve(request));
        const Grid& surface = world.layers.at("water_surface");
        const Grid& rain = world.layers.at("rainfall");
        const Grid& uplift = world.layers.at("wind_uplift");
        const int n = surface.size();

        // 90 degrees blows west to east and draws its air from the column to the west;
        // 270 degrees is the mirror of that.
        const int upwind_column = bearing < 180.0 ? -1 : 1;

        double windward_rain = 0.0, windward_uplift = 0.0, lee_rain = 0.0, lee_uplift = 0.0;
        int windward = 0, lee = 0;
        for (int z = 0; z < n; ++z) {
            // Inside 30 degrees of the equator a step of arc is at most 1.16 columns,
            // so the neighbouring column is a fair stand-in for where the air was.
            const double latitude = 90.0 - 180.0 * z / (n - 1);
            if (std::abs(latitude) > 30.0) continue;
            for (int x = 0; x < n - 1; ++x) {
                const int from = ((x + upwind_column) % (n - 1) + (n - 1)) % (n - 1);
                const double rise = surface.at(x, z) - surface.at(from, z);
                if (rise > 1.0) {  // the ground climbs into the arriving air
                    windward_rain += rain.at(x, z);
                    windward_uplift += uplift.at(x, z);
                    ++windward;
                } else if (rise < -1.0) {  // it drops away behind the crest
                    lee_rain += rain.at(x, z);
                    lee_uplift += uplift.at(x, z);
                    ++lee;
                }
            }
        }
        const std::string at = " at bearing " + std::to_string(static_cast<int>(bearing));
        check::is_true(windward > 100 && lee > 100,
                       "both kinds of slope to exist" + at + " (" + std::to_string(windward) + " rising, " +
                           std::to_string(lee) + " falling)");

        const double windward_mean_uplift = windward_uplift / windward;
        const double lee_mean_uplift = lee_uplift / lee;
        check::is_true(windward_mean_uplift > 20.0 * lee_mean_uplift,
                       "uplift to land on the slopes that climb into the wind" + at + " (" +
                           std::to_string(windward_mean_uplift) + " vs " + std::to_string(lee_mean_uplift) + ")");

        const double windward_mean_rain = windward_rain / windward;
        const double lee_mean_rain = lee_rain / lee;
        check::is_true(windward_mean_rain > 3.0 * lee_mean_rain,
                       "rain to follow it there" + at + " (" + std::to_string(windward_mean_rain) + " vs " +
                           std::to_string(lee_mean_rain) + ")");
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
