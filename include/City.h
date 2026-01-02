#pragma once

#include <vector>
#include <string>

/**
 * @file City.h
 *
 * Defines data structures representing the output of the procedural city
 * generator.  A City consists of a zoning grid, parcel-based buildings,
 * a set of facilities (hospitals, schools) and a collection of road
 * segments.  The generator populates these containers based on the
 * configuration supplied by the user.  Facilities are linked to parcels
 * but recorded separately for easy counting and querying.
 */

/// Enumeration of high‑level land‑use zones.
enum class ZoneType {
    None,        ///< Undeveloped (outside the city radius)
    Residential, ///< Residential areas (houses, apartments)
    Commercial,  ///< Commercial/business districts
    Industrial,  ///< Industrial zones (factories, warehouses)
    Green        ///< Parks, green spaces
};

/// Simple axis-aligned rectangle used for blocks and parcels.
struct Rect {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;

    double width() const { return x1 - x0; }
    double height() const { return y1 - y0; }
    double centreX() const { return (x0 + x1) * 0.5; }
    double centreY() const { return (y0 + y1) * 0.5; }
};

/// Representation of a single building placed on a parcel footprint.
struct Building {
    Rect footprint;          ///< Axis-aligned footprint polygon
    ZoneType zone = ZoneType::None;
    int height = 0;          ///< Height expressed in arbitrary storeys
    bool facility = false;   ///< True if this building hosts a public facility
};

/// Representation of a city block bounded by roads.
struct Block {
    Rect bounds;
};

/// Representation of a public facility such as a hospital or school.
struct Facility {
    /// Kinds of facilities supported by the generator.
    enum class Type { Hospital, School };
    double x = 0.0;
    double y = 0.0;
    Type type = Type::Hospital;
};

/// Classification of road hierarchy.  Used to vary rendered width.
enum class RoadType { Arterial, Secondary, Local };

/// Representation of a linear road segment.  Coordinates are expressed in
/// grid units; segments connect arbitrary points and can be used to
/// reconstruct the road network in a visualiser.
struct RoadSegment {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    RoadType type = RoadType::Local;
};

/// Width (in world units) associated with each road hierarchy level.
inline double roadWidth(RoadType type) {
    switch (type) {
        case RoadType::Arterial: return 1.6;
        case RoadType::Secondary: return 1.2;
        case RoadType::Local:
        default: return 0.8;
    }
}

/**
 * @brief Representation of an entire city.
 *
 * The City structure aggregates the outputs of the procedural generation
 * process.  It stores a zoning grid for statistics, a collection of
 * parcel-based Building footprints, Facilities and RoadSegments forming
 * the primary road network.  Helper methods are provided to index into the
 * zoning grid and to serialise the city into common formats (Wavefront OBJ
 * and JSON summary).
 */
class City {
public:
    /// Construct an empty city of the given grid size.  Zoning is
    /// initialised to undeveloped cells.
    explicit City(int size = 0);

    /// Grid dimension (city is size × size cells).
    int size = 0;

    /// Zoning grid expressed per underlying cell.  This is retained for
    /// statistics and to compute parcel zoning.
    std::vector<ZoneType> zones;

    /// Collection of parcel-based buildings (one per parcel).
    std::vector<Building> buildings;

    /// List of facilities (hospitals, schools) placed within the city.
    std::vector<Facility> facilities;

    /// Collection of road segments forming the primary road network.
    std::vector<RoadSegment> roads;

    /// Blocks carved out by the road network.
    std::vector<Block> blocks;

    /// Access zoning at coordinates (x, y).  No bounds checking is
    /// performed; callers should ensure indices are valid (0 ≤ x,y < size).
    ZoneType &zoneAt(int x, int y) {
        return zones[y * size + x];
    }

    /// Const overload of zoneAt().
    const ZoneType &zoneAt(int x, int y) const {
        return zones[y * size + x];
    }

    /**
     * @brief Write the city as a simple 3D model in Wavefront OBJ format.
     *
     * Each non‑green parcel footprint is represented as an axis‑aligned box
     * extruded vertically from the parcel base.  Green parcels contribute no
     * geometry, while undeveloped parcels (None) are ignored.  The OBJ file
     * contains vertices and triangular faces; materials are omitted for
     * simplicity.  Note that building height is scaled by 1.0 unit per
     * floor, but this can be adjusted by postprocessing.
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
