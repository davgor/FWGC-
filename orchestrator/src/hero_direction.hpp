// Heroes' deeds (contract version 26): the numbers the director decides them by. Internal to the
// orchestrator; README.md, "Heroes' deeds", says them in words.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace orchestrator::direction {

// A week between one hero's deeds.
inline constexpr double kHeroSpanDays = 7.0;
// The nests a hero defends a place against: the worst within this of it.
inline constexpr double kHeroThreatM = 5000.0;
// The most of a hero's own deeds since the stranger last saw them that ride in their packets (contract version 29).
inline constexpr std::size_t kOwnDeedsSince = 3;

// How much better a hero's chance is at their next deed after they took one resolved (contract version 30).
inline constexpr double kResolvedEdge = 0.1;

// The chance a hero's deed goes their way: their fame's band (notable 45%, renowned 60%, legendary 75%)
// against the danger they face (1 to 5), 8 points a step from 3, 15 less for an assault on a ruin, and
// `edge` more when they took their last deed resolved; never past 90%. The least it can be is 14%, a notable
// hero's assault against danger 5.
inline double hero_chance(const std::string& band, int danger, bool assault, double edge) {
    double chance = band == "legendary" ? 0.75 : band == "renowned" ? 0.6 : 0.45;
    chance -= 0.08 * static_cast<double>(danger - 3);
    if (assault) chance -= 0.15;
    return std::min(chance + edge, 0.9);
}

// A draw in [0, 1) from a hero's own seed, the day and what they do: a hash (splitmix64's finish), so the
// same world on the same day draws the same, and replay acts the same.
inline double hero_draw(std::uint32_t seed, double day, const std::string& action) {
    std::uint64_t x = (static_cast<std::uint64_t>(seed) << 32) ^ static_cast<std::uint64_t>(std::floor(day));
    for (const char c : action) x = (x ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0);
}

}  // namespace orchestrator::direction
