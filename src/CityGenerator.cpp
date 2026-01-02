#include "CityGenerator.h"

#include <random>
#include <cmath>
#include <algorithm>

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

// Determine a representative zone for the centre of a rectangle footprint.
static ZoneType sampleZone(const City &city, const Rect &r) {
    double cx = std::clamp(r.centreX(), 0.0, static_cast<double>(city.size - 1));
    double cy = std::clamp(r.centreY(), 0.0, static_cast<double>(city.size - 1));
    int ix = static_cast<int>(std::floor(cx));
    int iy = static_cast<int>(std::floor(cy));
    return city.zoneAt(ix, iy);
}

// Sample a height for a parcel based on its zone and footprint size.  Larger
// footprints tend to produce slightly taller buildings in commercial areas.
static int sampleHeight(ZoneType zone, const Rect &footprint, std::mt19937 &rng) {
    double area = std::max(footprint.width() * footprint.height(), 1.0);
    int height = 1;
    if (zone == ZoneType::Residential) {
        std::uniform_int_distribution<int> distH(2, 6);
        height = distH(rng);
    } else if (zone == ZoneType::Commercial) {
        std::uniform_int_distribution<int> distH(5, 18);
        height = distH(rng);
        height += static_cast<int>(std::min(area / 30.0, 5.0));
    } else if (zone == ZoneType::Industrial) {
        std::uniform_int_distribution<int> distH(3, 8);
        height = distH(rng);
    } else {
        height = 0;
    }
    return std::max(height, 0);
}

// Recursively subdivide a rectangle into smaller lots using a binary split
// along the longest dimension until parcels fit within maxSize.
static void subdivideRect(const Rect &r, double minSize, double maxSize,
                          std::mt19937 &rng, std::vector<Rect> &out, int depth = 0) {
    double w = r.width();
    double h = r.height();
    if ((w <= maxSize && h <= maxSize) || depth > 6) {
        out.push_back(r);
        return;
    }
    bool splitX = (w > h);
    double minCut = splitX ? r.x0 + minSize : r.y0 + minSize;
    double maxCut = splitX ? r.x1 - minSize : r.y1 - minSize;
    if (maxCut <= minCut) {
        out.push_back(r);
        return;
    }
    std::uniform_real_distribution<double> dist(minCut, maxCut);
    double cut = dist(rng);
    Rect a = r;
    Rect b = r;
    if (splitX) {
        a.x1 = cut;
        b.x0 = cut;
    } else {
        a.y1 = cut;
        b.y0 = cut;
    }
    subdivideRect(a, minSize, maxSize, rng, out, depth + 1);
    subdivideRect(b, minSize, maxSize, rng, out, depth + 1);
}

// Carve out a central courtyard from a block and subdivide the remaining
// strips into parcels.  If the block is too small for a courtyard, the whole
// area is subdivided.
static std::vector<Rect> parcelizeBlock(const Block &block, std::mt19937 &rng) {
    const Rect &b = block.bounds;
    double w = b.width();
    double h = b.height();
    const double minParcel = 3.0;
    const double maxParcel = 12.0;
    std::vector<Rect> parcels;
    // Randomised courtyard fraction; ensures at least ~15% stays open.
    std::uniform_real_distribution<double> fracDist(0.15, 0.30);
    double margin = std::min(w, h) * fracDist(rng);
    if (margin * 2.0 < w && margin * 2.0 < h) {
        Rect inner{b.x0 + margin, b.y0 + margin, b.x1 - margin, b.y1 - margin};
        Rect strips[4] = {
            {b.x0, b.y0, b.x1, inner.y0},
            {b.x0, inner.y1, b.x1, b.y1},
            {b.x0, inner.y0, inner.x0, inner.y1},
            {inner.x1, inner.y0, b.x1, inner.y1}
        };
        for (const auto &s : strips) {
            if (s.width() >= minParcel && s.height() >= minParcel) {
                subdivideRect(s, minParcel, maxParcel, rng, parcels);
            }
        }
        // The inner courtyard is intentionally left empty.
    } else {
        subdivideRect(b, minParcel, maxParcel, rng, parcels);
    }
    return parcels;
}

} // anonymous namespace

City CityGenerator::generate(const Config &cfg) {
    City city(cfg.grid_size);
    int size = cfg.grid_size;
    double centre = static_cast<double>(size) / 2.0;
    double radius = (static_cast<double>(size) * cfg.city_radius) / 2.0;
    // RNG for various choices
    std::mt19937 rng(cfg.seed);
    // 1. Zone assignment across the base grid
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            double dx = static_cast<double>(x) + 0.5 - centre;
            double dy = static_cast<double>(y) + 0.5 - centre;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist > radius) {
                city.zoneAt(x, y) = ZoneType::None;
                continue;
            }
            double value = fractalNoise(x, y, cfg.seed);
            if (value < 0.55) {
                city.zoneAt(x, y) = ZoneType::Residential;
            } else if (value < 0.75) {
                city.zoneAt(x, y) = ZoneType::Commercial;
            } else if (value < 0.90) {
                city.zoneAt(x, y) = ZoneType::Industrial;
            } else {
                city.zoneAt(x, y) = ZoneType::Green;
            }
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
    for (const auto z : city.zones) {
        if (z == ZoneType::Green) currentGreen++;
    }
    if (currentGreen < targetGreenCells) {
        // Determine how many additional cells we need to convert
        std::uint64_t diff = targetGreenCells - currentGreen;
        // Collect candidate indices
        std::vector<std::size_t> candidates;
        candidates.reserve(city.zones.size());
        for (std::size_t idx = 0; idx < city.zones.size(); ++idx) {
            ZoneType z = city.zones[idx];
            if (z == ZoneType::Residential || z == ZoneType::Industrial) {
                candidates.push_back(idx);
            }
        }
        // Shuffle candidates deterministically using rng
        std::shuffle(candidates.begin(), candidates.end(), rng);
        std::size_t converted = 0;
        for (std::size_t i = 0; i < candidates.size() && converted < diff; ++i) {
            std::size_t idx = candidates[i];
            city.zones[idx] = ZoneType::Green;
            converted++;
        }
    }
    // 3. Generate primary road network
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
    // 4. Derive blocks from road lines (axis-aligned grid between road traces)
    std::vector<double> xLines = {cx - radius, cx - radius * 0.9, cx - radius * 0.5,
                                  cx, cx + radius * 0.5, cx + radius * 0.9, cx + radius};
    std::vector<double> yLines = {cy - radius, cy - radius * 0.9, cy - radius * 0.5,
                                  cy, cy + radius * 0.5, cy + radius * 0.9, cy + radius};
    auto uniqSort = [](std::vector<double> &vals) {
        std::sort(vals.begin(), vals.end());
        vals.erase(std::unique(vals.begin(), vals.end()), vals.end());
    };
    uniqSort(xLines);
    uniqSort(yLines);
    for (std::size_t xi = 0; xi + 1 < xLines.size(); ++xi) {
        for (std::size_t yi = 0; yi + 1 < yLines.size(); ++yi) {
            Rect bounds{xLines[xi], yLines[yi], xLines[xi + 1], yLines[yi + 1]};
            double blockCx = bounds.centreX();
            double blockCy = bounds.centreY();
            double dx = blockCx - cx;
            double dy = blockCy - cy;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist > radius * 1.05) continue; // outside developed area
            if (bounds.width() < 1.0 || bounds.height() < 1.0) continue;
            city.blocks.push_back({bounds});
        }
    }
    // 5. Subdivide blocks into parcels and spawn buildings per parcel
    for (const auto &block : city.blocks) {
        std::vector<Rect> parcels = parcelizeBlock(block, rng);
        for (const auto &footprint : parcels) {
            double cxp = footprint.centreX();
            double cyp = footprint.centreY();
            double dx = cxp - cx;
            double dy = cyp - cy;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist > radius * 1.02) continue;
            ZoneType z = sampleZone(city, footprint);
            if (z == ZoneType::None) continue;
            Building b;
            b.footprint = footprint;
            b.zone = z;
            b.height = sampleHeight(z, footprint, rng);
            b.facility = false;
            // If the parcel overlaps predominantly green cells, downgrade to green
            if (z == ZoneType::Green) {
                b.height = 0;
            }
            city.buildings.push_back(b);
        }
    }
    // 6. Place facilities (hospitals and schools) on suitable parcels
    std::vector<std::size_t> eligibleParcels;
    for (std::size_t i = 0; i < city.buildings.size(); ++i) {
        const auto &b = city.buildings[i];
        if (b.zone == ZoneType::Residential || b.zone == ZoneType::Commercial) {
            eligibleParcels.push_back(i);
        }
    }
    if (eligibleParcels.empty()) {
        for (std::size_t i = 0; i < city.buildings.size(); ++i) eligibleParcels.push_back(i);
    }
    std::shuffle(eligibleParcels.begin(), eligibleParcels.end(), rng);
    auto placeFacilities = [&](Facility::Type type, std::uint32_t count) {
        std::uint32_t placed = 0;
        for (std::size_t idx : eligibleParcels) {
            if (placed >= count) break;
            Building &b = city.buildings[idx];
            if (!b.facility) {
                b.facility = true;
                Facility f;
                f.x = b.footprint.centreX();
                f.y = b.footprint.centreY();
                f.type = type;
                city.facilities.push_back(f);
                placed++;
            }
        }
    };
    placeFacilities(Facility::Type::Hospital, cfg.hospitals);
    placeFacilities(Facility::Type::School, cfg.schools);
    return city;
}
