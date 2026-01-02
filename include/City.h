#pragma once

#include <vector>
#include <string>

/**
 * @file City.h
 *
 * Defines data structures representing the output of the procedural city
 * generator.  A City consists of a square grid of building lots, a set of
 * facilities (hospitals, schools) and a collection of road segments.  The
 * generator populates these containers based on the configuration supplied
 * by the user.  Facilities are treated specially: they occupy a grid cell
 * but have their own type separate from the zone.
 */

/// Enumeration of high‑level land‑use zones.
enum class ZoneType {
    None,        ///< Undeveloped (outside the city radius)
    Residential, ///< Residential areas (houses, apartments)
    Commercial,  ///< Commercial/business districts
    Industrial,  ///< Industrial zones (factories, warehouses)
    Green        ///< Parks, green spaces
};

/// Representation of a single building placed on a lot.
struct Building {
    int x = 0;
    int y = 0;
    ZoneType zone = ZoneType::None;
    int height = 0;      ///< Height expressed in arbitrary storeys
    bool facility = false; ///< True if this building hosts a public facility
};

/// Representation of a public facility such as a hospital or school.
struct Facility {
    /// Kinds of facilities supported by the generator.
    enum class Type { Hospital, School };
    int x = 0;
    int y = 0;
    Type type = Type::Hospital;
};

/// Representation of a linear road segment.  Coordinates are expressed in
/// grid units; segments connect arbitrary points and can be used to
/// reconstruct the road network in a visualiser.
struct RoadSegment {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
};

/**
 * @brief Representation of an entire city.
 *
 * The City structure aggregates the outputs of the procedural generation
 * process.  It stores a flattened list of Building objects (one per grid
 * cell), a list of Facility objects representing social services, and a
 * collection of RoadSegments representing the high‑level transportation
 * network.  Helper methods are provided to index into the building grid,
 * and to serialise the city into common formats (Wavefront OBJ and JSON
 * summary).
 */
class City {
public:
    /// Construct an empty city of the given grid size.  Buildings will be
    /// initialised as undeveloped cells.
    explicit City(int size = 0);

    /// Grid dimension (city is size × size cells).
    int size = 0;

    /// Flattened array of building lots.  The element at (x,y) can be
    /// accessed via buildings[y * size + x].  If zone == None, the cell
    /// lies outside the developed area.
    std::vector<Building> buildings;

    /// List of facilities (hospitals, schools) placed within the city.
    std::vector<Facility> facilities;

    /// Collection of road segments forming the primary road network.
    std::vector<RoadSegment> roads;

    /// Access a building cell at coordinates (x, y).  No bounds checking is
    /// performed; callers should ensure indices are valid (0 ≤ x,y < size).
    Building &at(int x, int y) {
        return buildings[y * size + x];
    }

    /// Const overload of at().
    const Building &at(int x, int y) const {
        return buildings[y * size + x];
    }

    /**
     * @brief Write the city as a simple 3D model in Wavefront OBJ format.
     *
     * Each non‑green building lot is represented as an axis‑aligned box
     * extruded vertically from the grid cell's base.  Green cells (parks)
     * contribute no geometry, while undeveloped cells (None) are ignored.
     * The OBJ file contains vertices and triangular faces; materials are
     * omitted for simplicity.  Note that building height is scaled by 1.0
     * unit per floor, but this can be adjusted by postprocessing.
     *
     * @param filename Path to the OBJ file to create.
     */
    void saveOBJ(const std::string &filename) const;

    /**
     * @brief Write a JSON file summarising high‑level statistics of the city.
     *
     * The summary includes counts of buildings by zone, number of facilities,
     * and other metrics.  This function is primarily used by integration
     * tests to verify correctness and scaling.  The JSON is emitted using
     * manual string concatenation to avoid external dependencies.
     *
     * @param filename Path to the JSON file to create.
     */
    void saveSummary(const std::string &filename) const;
};
