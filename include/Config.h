#pragma once

#include <cstdint>
#include <string>
#include <algorithm>
#include <stdexcept>

/**
 * @brief High-level configuration for procedural city generation.
 *
 * This struct is intentionally simple and POD-like so it can be:
 *  - filled from Python
 *  - serialized/deserialized
 *  - extended without ABI pain
 */
struct Config {
    // ===== Determinism =====
    std::uint32_t seed = 0;

    // ===== City scale =====
    int population = 100000;

    // Square grid resolution (NxN cells)
    int grid_size = 100;

    // Radius of the "urbanized" area in normalized [0,1] coordinates
    double city_radius = 0.8;

    // ===== Social infrastructure =====
    int hospitals = 1;
    int schools = 5;

    // ===== Urban planning parameters =====
    // Minimal green area per person (m^2)
    double green_m2_per_capita = 8.0;

    // ===== Transport =====
    enum class TransportMode {
        Car,
        PublicTransit,
        Walk
    };

    TransportMode transport_mode = TransportMode::Car;

    // ===== Output =====
    std::string output_prefix = "city";

    // ===== Sanity checks =====
    void normalize() {
        if (population < 0) population = 0;
        if (grid_size < 10) grid_size = 10;
        if (city_radius <= 0.0) city_radius = 0.1;
        if (city_radius > 1.0) city_radius = 1.0;
        if (hospitals < 0) hospitals = 0;
        if (schools < 0) schools = 0;
        if (green_m2_per_capita < 0.0) green_m2_per_capita = 0.0;
    }
};

inline Config::TransportMode transportModeFromString(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    if (s == "car") return Config::TransportMode::Car;
    if (s == "public" || s == "public_transit" || s == "transit")
        return Config::TransportMode::PublicTransit;
    if (s == "walk" || s == "pedestrian")
        return Config::TransportMode::Walk;

    throw std::invalid_argument("Unknown transport mode: " + s);
}