#include "CityGenerator.h"

#include <random>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace {

// Hash-based pseudo-random noise for integer coordinates.  Uses bit
// manipulation to produce repeatable pseudo-random values in [0,1).
static double noise(int x, int y, std::uint32_t seed) {
    // Compute a simple 32-bit hash based on coordinates and seed.  The
    // constants are arbitrary primes chosen to decorrelate bits.  We avoid
    // std::hash for portability.
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u;
    h += static_cast<std::uint32_t>(y) * 668265263u;
    h ^= seed + 0x9e3779b9u + (h << 6) + (h >> 2);
    // Final mix
    h ^= (h >> 17);
    h *= 0xed5ad4bbU;
    h ^= (h >> 11);
    h *= 0xac4c1b51U;
    h ^= (h >> 15);
    // Scale to [0,1)
    return (h & 0xFFFFFFu) / static_cast<double>(0x1000000u);
}

// Fractal noise combining multiple octaves.  Higher octaves add finer
// detail.  Frequencies and amplitudes follow a common pattern: each
// successive octave doubles the frequency and halves the amplitude.
static double fractalNoise(int x, int y, std::uint32_t seed, int octaves = 4) {
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double amplitudeSum = 0.0;
    for (int i = 0; i < octaves; ++i) {
        // Sample noise at scaled coordinates; cast to int to avoid large
        // floating point increments (coarse sampling is acceptable here).
        int sx = static_cast<int>(x * frequency);
        int sy = static_cast<int>(y * frequency);
        double n = noise(sx, sy, seed + static_cast<std::uint32_t>(i) * 17u);
        sum += amplitude * n;
        amplitudeSum += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return sum / amplitudeSum;
}

// Convert a linear index into a 2D coordinate pair (x,y).
inline std::pair<int,int> indexToCoord(std::size_t idx, int size) {
    int y = static_cast<int>(idx / size);
    int x = static_cast<int>(idx % size);
    return {x, y};
}

} // anonymous namespace

City CityGenerator::generate(const Config &cfg) {
    City city(cfg.grid_size);
    int size = cfg.grid_size;
    double centre = static_cast<double>(size) / 2.0;
    double radius = (static_cast<double>(size) * cfg.city_radius) / 2.0;
    // RNG for various choices
    std::mt19937 rng(cfg.seed);
    // 1. Zone assignment and building heights
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            Building &b = city.at(x, y);
            double dx = static_cast<double>(x) + 0.5 - centre;
            double dy = static_cast<double>(y) + 0.5 - centre;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist > radius) {
                b.zone = ZoneType::None;
                b.height = 0;
                continue;
            }
            // Compute noise value in [0,1]
            double value = fractalNoise(x, y, cfg.seed);
            // Assign zone based on value thresholds.  These thresholds were
            // chosen empirically to produce a mix of zones; they can be
            // adjusted to favour certain land uses.
            if (value < 0.55) {
                b.zone = ZoneType::Residential;
            } else if (value < 0.75) {
                b.zone = ZoneType::Commercial;
            } else if (value < 0.90) {
                b.zone = ZoneType::Industrial;
            } else {
                b.zone = ZoneType::Green;
            }
            // Assign building height by zone
            int h = 0;
            if (b.zone == ZoneType::Residential) {
                std::uniform_int_distribution<int> distH(2, 6);
                h = distH(rng);
            } else if (b.zone == ZoneType::Commercial) {
                std::uniform_int_distribution<int> distH(5, 20);
                h = distH(rng);
            } else if (b.zone == ZoneType::Industrial) {
                std::uniform_int_distribution<int> distH(3, 6);
                h = distH(rng);
            } else if (b.zone == ZoneType::Green) {
                h = 0;
            }
            b.height = h;
            b.facility = false;
        }
    }
    // 2. Ensure a minimum amount of green space based on population
    // The recommended minimum is about 8 m^2 per inhabitant.  Each grid
    // cell represents an arbitrary area; we assume each cell could be ~100 m ×
    // 100 m (10,000 m²).  So one cell contributes 10,000 m² of green space.
    // Compute the target number of green cells and convert some cells if
    // necessary.  Choose candidates from residential and industrial zones.
    double greenAreaPerPerson = 8.0; // m^2 per person
    double cellArea = 100.0 * 100.0; // m^2 per cell
    std::uint64_t targetGreenCells = static_cast<std::uint64_t>(
        std::ceil((cfg.population * greenAreaPerPerson) / cellArea));
    // Count current green cells
    std::uint64_t currentGreen = 0;
    for (const auto &b : city.buildings) {
        if (b.zone == ZoneType::Green) currentGreen++;
    }
    if (currentGreen < targetGreenCells) {
        // Determine how many additional cells we need to convert
        std::uint64_t diff = targetGreenCells - currentGreen;
        // Collect candidate indices
        std::vector<std::size_t> candidates;
        candidates.reserve(city.buildings.size());
        for (std::size_t idx = 0; idx < city.buildings.size(); ++idx) {
            const Building &b = city.buildings[idx];
            if (b.zone == ZoneType::Residential || b.zone == ZoneType::Industrial) {
                candidates.push_back(idx);
            }
        }
        // Shuffle candidates deterministically using rng
        std::shuffle(candidates.begin(), candidates.end(), rng);
        std::size_t converted = 0;
        for (std::size_t i = 0; i < candidates.size() && converted < diff; ++i) {
            std::size_t idx = candidates[i];
            Building &b = city.buildings[idx];
            b.zone = ZoneType::Green;
            b.height = 0;
            b.facility = false;
            converted++;
        }
    }
    // 3. Place facilities (hospitals and schools)
    // Collect eligible positions (residential or commercial cells)
    std::vector<std::size_t> eligible;
    eligible.reserve(city.buildings.size());
    for (std::size_t idx = 0; idx < city.buildings.size(); ++idx) {
        const Building &b = city.buildings[idx];
        if (b.zone == ZoneType::Residential || b.zone == ZoneType::Commercial) {
            eligible.push_back(idx);
        }
    }
    // Shuffle once
    std::shuffle(eligible.begin(), eligible.end(), rng);
    // Helper to place a facility of a given type
    auto placeFacilities = [&](Facility::Type type, std::uint32_t count) {
        std::uint32_t placed = 0;
        for (std::size_t i = 0; i < eligible.size() && placed < count; ++i) {
            std::size_t idx = eligible[i];
            Building &b = city.buildings[idx];
            if (!b.facility) {
                b.facility = true;
                // Facilities are treated like buildings but flagged
                Facility f;
                auto [fx, fy] = indexToCoord(idx, city.size);
                f.x = fx;
                f.y = fy;
                f.type = type;
                city.facilities.push_back(f);
                placed++;
            }
        }
    };
    placeFacilities(Facility::Type::Hospital, cfg.hospitals);
    placeFacilities(Facility::Type::School, cfg.schools);
    // 4. Generate primary road network
    // Vertical and horizontal main roads crossing the centre
    double cx = centre;
    double cy = centre;
    // Vertical line from top to bottom of developed area
    city.roads.push_back({cx, cy - radius, cx, cy + radius});
    // Horizontal line from left to right
    city.roads.push_back({cx - radius, cy, cx + radius, cy});
    // Two ring roads at 50% and 90% of the developed radius
    auto addRing = [&](double r) {
        double x0 = cx - r;
        double x1 = cx + r;
        double y0 = cy - r;
        double y1 = cy + r;
        // top
        city.roads.push_back({x0, y0, x1, y0});
        // right
        city.roads.push_back({x1, y0, x1, y1});
        // bottom
        city.roads.push_back({x1, y1, x0, y1});
        // left
        city.roads.push_back({x0, y1, x0, y0});
    };
    addRing(radius * 0.5);
    addRing(radius * 0.9);
    return city;
}
