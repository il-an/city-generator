#pragma once

#include "Config.h"
#include "City.h"

/**
 * @file CityGenerator.h
 *
 * Declares the high‑level entry point for generating procedural cities.  The
 * CityGenerator encapsulates all the algorithmic steps required to create
 * a complete City from a Config, including terrain/noise synthesis,
 * zoning, road layout, building placement, facility distribution and
 * compliance with basic urban planning rules.
 */

class CityGenerator {
public:
    /**
     * @brief Generate a city based on the provided configuration.
     *
     * The returned City contains a discretised representation of all
     * buildings, facilities and roads.  Generation is deterministic for a
     * given Config (especially the seed value).  See the implementation
     * (CityGenerator.cpp) for details of the algorithm.
     *
     * @param cfg Configuration controlling the generation process.
     * @return Generated City object.
     */
    static City generate(const Config &cfg);
};
