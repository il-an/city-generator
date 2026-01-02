#include "CityGenerator.h"

#include <random>
#include <cmath>
#include <algorithm>
#include <limits>
#include <array>

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
static int sampleHeight(ZoneType zone, const Rect &footprint, double distToCentre,
                        double cityRadius, std::mt19937 &rng) {
    double area = std::max(footprint.width() * footprint.height(), 1.0);
    double radial = 1.0 - std::clamp(distToCentre / std::max(cityRadius, 1e-6), 0.0, 1.0);
    auto clampHeight = [](double h, int minH, int maxH) {
        int v = static_cast<int>(std::round(h));
        return std::clamp(v, minH, maxH);
    };
    switch (zone) {
        case ZoneType::Residential: {
            std::lognormal_distribution<double> distH(std::log(3.0), 0.35);
            double h = distH(rng);
            h *= 0.6 + 0.7 * radial; // taller near centre, modest elsewhere
            h += std::min(std::sqrt(area) * 0.1, 1.5);
            return clampHeight(h, 2, 12);
        }
        case ZoneType::Commercial: {
            std::lognormal_distribution<double> distH(std::log(8.0), 0.5);
            double h = distH(rng);
            h *= 0.8 + 1.2 * radial; // CBD bias
            h += std::min(std::sqrt(area) * 0.15, 3.0);
            return clampHeight(h, 4, 40);
        }
        case ZoneType::Industrial: {
            std::exponential_distribution<double> distH(1.0 / 5.0);
            double h = 2.0 + distH(rng);
            h *= 0.7 + 0.6 * radial;
            h += std::min(std::sqrt(area) * 0.05, 1.0);
            return clampHeight(h, 2, 14);
        }
        default:
            return 0;
    }
}

// Shrink a parcel footprint and apply small random jitter so buildings do not
// perfectly fill or align within their parcels.
static Rect jitterFootprint(const Rect &parcel, std::mt19937 &rng) {
    double w = parcel.width();
    double h = parcel.height();
    if (w <= 0.0 || h <= 0.0) return parcel;
    std::uniform_real_distribution<double> scaleDist(0.4, 0.9);
    double areaScale = scaleDist(rng);
    double linearScale = std::sqrt(areaScale);
    double newW = w * linearScale;
    double newH = h * linearScale;
    double marginX = (w - newW) * 0.5;
    double marginY = (h - newH) * 0.5;
    double jitterFrac = 0.6;
    std::uniform_real_distribution<double> jitterX(-marginX * jitterFrac, marginX * jitterFrac);
    std::uniform_real_distribution<double> jitterY(-marginY * jitterFrac, marginY * jitterFrac);
    double cx = parcel.centreX() + jitterX(rng);
    double cy = parcel.centreY() + jitterY(rng);
    Rect r;
    r.x0 = cx - newW * 0.5;
    r.x1 = cx + newW * 0.5;
    r.y0 = cy - newH * 0.5;
    r.y1 = cy + newH * 0.5;
    // Clamp to stay within the parcel bounds
    double shiftX0 = std::max(parcel.x0 - r.x0, 0.0);
    double shiftY0 = std::max(parcel.y0 - r.y0, 0.0);
    double shiftX1 = std::max(r.x1 - parcel.x1, 0.0);
    double shiftY1 = std::max(r.y1 - parcel.y1, 0.0);
    r.x0 += shiftX0 - shiftX1;
    r.x1 += shiftX0 - shiftX1;
    r.y0 += shiftY0 - shiftY1;
    r.y1 += shiftY0 - shiftY1;
    return r;
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

static std::array<Vec2, 4> rectToQuad(const Rect &r) {
    return {{
        {r.x0, r.y0},
        {r.x1, r.y0},
        {r.x1, r.y1},
        {r.x0, r.y1}
    }};
}

static Rect boundsFromQuad(const std::array<Vec2, 4> &q) {
    Rect r;
    r.x0 = r.x1 = q[0].x;
    r.y0 = r.y1 = q[0].y;
    for (int i = 1; i < 4; ++i) {
        r.x0 = std::min(r.x0, q[i].x);
        r.x1 = std::max(r.x1, q[i].x);
        r.y0 = std::min(r.y0, q[i].y);
        r.y1 = std::max(r.y1, q[i].y);
    }
    return r;
}

static Vec2 centroidOfQuad(const std::array<Vec2, 4> &q) {
    double cx = 0.0;
    double cy = 0.0;
    for (const auto &p : q) {
        cx += p.x;
        cy += p.y;
    }
    cx *= 0.25;
    cy *= 0.25;
    return {cx, cy};
}

static Vec2 polarToCartesian(double cx, double cy, double r, double theta) {
    double x = cx + r * std::cos(theta);
    double y = cy + r * std::sin(theta);
    return {x, y};
}

// Convert a wedge block into quads by unwrapping to a rectangle in (arc, radius)
// space, parcelising, and mapping back to polar coordinates.
static std::vector<std::array<Vec2, 4>> parcelizeWedge(double cx, double cy,
                                                       double r0, double r1,
                                                       double theta0, double theta1,
                                                       std::mt19937 &rng) {
    double radialThickness = r1 - r0;
    if (radialThickness <= 0.1) return {};
    double midR = (r0 + r1) * 0.5;
    double thetaSpan = theta1 - theta0;
    if (thetaSpan <= 1e-4 || midR <= 1e-6) return {};
    double arcLength = thetaSpan * midR;
    Rect uvBlock{0.0, 0.0, arcLength, radialThickness};
    std::vector<Rect> uvParcels;
    const double minParcel = 3.0;
    const double maxParcel = 12.0;
    subdivideRect(uvBlock, minParcel, maxParcel, rng, uvParcels);
    std::vector<std::array<Vec2, 4>> quads;
    quads.reserve(uvParcels.size());
    for (const auto &uv : uvParcels) {
        Rect jittered = jitterFootprint(uv, rng);
        double u0 = jittered.x0;
        double u1 = jittered.x1;
        double v0 = jittered.y0;
        double v1 = jittered.y1;
        auto uvToWorld = [&](double u, double v) {
            double t = theta0 + (u / arcLength) * thetaSpan;
            double rr = r0 + v;
            return polarToCartesian(cx, cy, rr, t);
        };
        std::array<Vec2, 4> quad = {{
            uvToWorld(u0, v0),
            uvToWorld(u1, v0),
            uvToWorld(u1, v1),
            uvToWorld(u0, v1)
        }};
        quads.push_back(quad);
    }
    return quads;
}

// Compute the shortest distance from a parcel to the road network.  Roads are
// treated as thickened line segments (using their hierarchy width) so parcels
// adjacent to roads yield zero distance.
static double distanceToRoads(const Rect &parcel, const std::vector<RoadSegment> &roads) {
    double best = std::numeric_limits<double>::max();
    for (const auto &road : roads) {
        double halfWidth = 0.5 * roadWidth(road.type);
        double minX = std::min(road.x1, road.x2) - halfWidth;
        double maxX = std::max(road.x1, road.x2) + halfWidth;
        double minY = std::min(road.y1, road.y2) - halfWidth;
        double maxY = std::max(road.y1, road.y2) + halfWidth;
        double dx = 0.0;
        if (parcel.x1 < minX) dx = minX - parcel.x1;
        else if (parcel.x0 > maxX) dx = parcel.x0 - maxX;
        double dy = 0.0;
        if (parcel.y1 < minY) dy = minY - parcel.y1;
        else if (parcel.y0 > maxY) dy = parcel.y0 - maxY;
        double dist = (dx == 0.0 || dy == 0.0) ? std::max(dx, dy) : std::sqrt(dx * dx + dy * dy);
        if (dist < best) best = dist;
    }
    return best;
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
    // 3. Generate primary road network and parcels according to layout
    double cx = centre;
    double cy = centre;
    if (cfg.layout == Config::LayoutType::Grid) {
        // Road alignments along fixed grid lines; these are reused when carving
        // blocks so that road geometry and parcels stay consistent.
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
        auto classifyRoad = [&](double coord, bool isX) {
            double anchor = isX ? cx : cy;
            double denom = (radius > 1e-6) ? radius : 1.0;
            double norm = std::abs(coord - anchor) / denom;
            if (norm < 0.15) return RoadType::Arterial;
            if (norm < 0.6) return RoadType::Secondary;
            return RoadType::Local;
        };
        auto addRoad = [&](double x0, double y0, double x1, double y1, RoadType t) {
            city.roads.push_back({x0, y0, x1, y1, t});
        };
        // Vertical and horizontal lines spanning the developed area.  Widths are
        // derived from hierarchy.
        for (double x : xLines) {
            RoadType type = classifyRoad(x, true);
            addRoad(x, cy - radius, x, cy + radius, type);
        }
        for (double y : yLines) {
            RoadType type = classifyRoad(y, false);
            addRoad(cx - radius, y, cx + radius, y, type);
        }
        // 4. Derive blocks from road lines (axis-aligned grid between road traces)
        auto insetFor = [&](double coord, bool isX) {
            return 0.5 * roadWidth(classifyRoad(coord, isX));
        };
        for (std::size_t xi = 0; xi + 1 < xLines.size(); ++xi) {
            for (std::size_t yi = 0; yi + 1 < yLines.size(); ++yi) {
                double x0 = xLines[xi] + insetFor(xLines[xi], true);
                double x1 = xLines[xi + 1] - insetFor(xLines[xi + 1], true);
                double y0 = yLines[yi] + insetFor(yLines[yi], false);
                double y1 = yLines[yi + 1] - insetFor(yLines[yi + 1], false);
                if (x1 <= x0 || y1 <= y0) continue;
                Rect bounds{x0, y0, x1, y1};
                double blockCx = bounds.centreX();
                double blockCy = bounds.centreY();
                double dx = blockCx - cx;
                double dy = blockCy - cy;
                double dist = std::sqrt(dx * dx + dy * dy);
                if (dist > radius * 1.05) continue; // outside developed area
                if (bounds.width() < 1.0 || bounds.height() < 1.0) continue;
                Block blk;
                blk.bounds = bounds;
                blk.hasCorners = true;
                blk.corners = rectToQuad(bounds);
                city.blocks.push_back(blk);
            }
        }
        // 5. Subdivide blocks into parcels and spawn buildings per parcel
        for (const auto &block : city.blocks) {
            std::vector<Rect> parcels = parcelizeBlock(block, rng);
            for (const auto &footprint : parcels) {
                Rect adjusted = jitterFootprint(footprint, rng);
                double cxp = adjusted.centreX();
                double cyp = adjusted.centreY();
                double dx = cxp - cx;
                double dy = cyp - cy;
                double dist = std::sqrt(dx * dx + dy * dy);
                if (dist > radius * 1.02) continue;
                ZoneType z = sampleZone(city, adjusted);
                if (z == ZoneType::None) continue;
                Building b;
                b.footprint = adjusted;
                b.zone = z;
                b.height = sampleHeight(z, adjusted, dist, radius, rng);
                b.facility = false;
                b.hasCorners = true;
                b.corners = rectToQuad(adjusted);
                // If the parcel overlaps predominantly green cells, downgrade to green
                if (z == ZoneType::Green) {
                    b.height = 0;
                }
                city.buildings.push_back(b);
            }
        }
    } else { // Radial layout
        int ringCount = std::clamp(static_cast<int>(std::round(3.0 + cfg.population / 200000.0)), 3, 8);
        int radialRoads = std::clamp(static_cast<int>(std::round(10.0 + cfg.city_radius * 8.0)), 8, 20);
        double maxR = radius;
        std::vector<double> ringEdges;
        ringEdges.reserve(ringCount + 2);
        ringEdges.push_back(0.0);
        for (int i = 1; i <= ringCount; ++i) {
            double frac = static_cast<double>(i) / static_cast<double>(ringCount + 1);
            ringEdges.push_back(maxR * frac);
        }
        ringEdges.push_back(maxR);
        std::sort(ringEdges.begin(), ringEdges.end());
        ringEdges.erase(std::unique(ringEdges.begin(), ringEdges.end()), ringEdges.end());
        std::vector<double> angles(radialRoads + 1);
        const double twoPi = 6.28318530717958647692;
        double delta = twoPi / static_cast<double>(radialRoads);
        for (int i = 0; i <= radialRoads; ++i) {
            angles[i] = delta * static_cast<double>(i);
        }
        auto ringType = [&](double r) {
            double norm = (maxR > 1e-6) ? (r / maxR) : 0.0;
            if (norm < 0.3) return RoadType::Arterial;
            if (norm < 0.75) return RoadType::Secondary;
            return RoadType::Local;
        };
        // Ring roads (approximated by segmented polylines)
        for (std::size_t ri = 1; ri + 1 < ringEdges.size(); ++ri) {
            double r = ringEdges[ri];
            int segs = std::max(32, radialRoads * 2);
            for (int s = 0; s < segs; ++s) {
                double t0 = twoPi * static_cast<double>(s) / static_cast<double>(segs);
                double t1 = twoPi * static_cast<double>(s + 1) / static_cast<double>(segs);
                Vec2 p0 = polarToCartesian(cx, cy, r, t0);
                Vec2 p1 = polarToCartesian(cx, cy, r, t1);
                city.roads.push_back({p0.x, p0.y, p1.x, p1.y, ringType(r)});
            }
        }
        // Radial arterials
        for (int i = 0; i < radialRoads; ++i) {
            double t = angles[i];
            Vec2 p0 = polarToCartesian(cx, cy, 0.0, t);
            Vec2 p1 = polarToCartesian(cx, cy, maxR, t);
            city.roads.push_back({p0.x, p0.y, p1.x, p1.y, RoadType::Arterial});
        }
        // Blocks: wedges defined by consecutive ring bands and angular sectors
        for (std::size_t ri = 0; ri + 1 < ringEdges.size(); ++ri) {
            double r0 = ringEdges[ri];
            double r1 = ringEdges[ri + 1];
            for (int si = 0; si < radialRoads; ++si) {
                double a0 = angles[si];
                double a1 = angles[si + 1];
                std::array<Vec2, 4> corners = {{
                    polarToCartesian(cx, cy, r0, a0),
                    polarToCartesian(cx, cy, r1, a0),
                    polarToCartesian(cx, cy, r1, a1),
                    polarToCartesian(cx, cy, r0, a1)
                }};
                Rect bounds = boundsFromQuad(corners);
                Vec2 blockC = centroidOfQuad(corners);
                double dx = blockC.x - cx;
                double dy = blockC.y - cy;
                double dist = std::sqrt(dx * dx + dy * dy);
                if (dist > radius * 1.1) continue;
                Block blk;
                blk.bounds = bounds;
                blk.hasCorners = true;
                blk.corners = corners;
                city.blocks.push_back(blk);
                auto parcels = parcelizeWedge(cx, cy, r0, r1, a0, a1, rng);
                for (const auto &quad : parcels) {
                    Rect parcelBounds = boundsFromQuad(quad);
                    Vec2 centreP = centroidOfQuad(quad);
                    double pdx = centreP.x - cx;
                    double pdy = centreP.y - cy;
                    double pdist = std::sqrt(pdx * pdx + pdy * pdy);
                    if (pdist > radius * 1.05) continue;
                    ZoneType z = sampleZone(city, parcelBounds);
                    if (z == ZoneType::None) continue;
                    Building b;
                    b.footprint = parcelBounds;
                    b.corners = quad;
                    b.hasCorners = true;
                    b.zone = z;
                    b.height = sampleHeight(z, parcelBounds, pdist, radius, rng);
                    b.facility = false;
                    if (z == ZoneType::Green) {
                        b.height = 0;
                    }
                    city.buildings.push_back(b);
                }
            }
        }
    }
    // 6. Place facilities (hospitals and schools) on suitable parcels
    struct ParcelCandidate {
        std::size_t idx;
        double roadDistance;
    };
    std::vector<ParcelCandidate> candidates;
    candidates.reserve(city.buildings.size());
    for (std::size_t i = 0; i < city.buildings.size(); ++i) {
        const auto &b = city.buildings[i];
        if (b.zone == ZoneType::Residential || b.zone == ZoneType::Commercial) {
            double dist = distanceToRoads(b.footprint, city.roads);
            candidates.push_back({i, dist});
        }
    }
    if (candidates.empty()) {
        for (std::size_t i = 0; i < city.buildings.size(); ++i) {
            double dist = distanceToRoads(city.buildings[i].footprint, city.roads);
            candidates.push_back({i, dist});
        }
    }
    std::vector<ParcelCandidate> nearRoads;
    std::vector<ParcelCandidate> interior;
    const double accessibleRadius = 1.6; // one arterial lane away from the carriageway
    for (const auto &c : candidates) {
        if (c.roadDistance <= accessibleRadius) nearRoads.push_back(c);
        else interior.push_back(c);
    }
    auto sortByAccess = [&](std::vector<ParcelCandidate> &vec) {
        std::shuffle(vec.begin(), vec.end(), rng);
        std::sort(vec.begin(), vec.end(),
                  [](const ParcelCandidate &a, const ParcelCandidate &b) {
                      return a.roadDistance < b.roadDistance;
                  });
    };
    sortByAccess(nearRoads);
    sortByAccess(interior);
    std::vector<std::size_t> orderedParcels;
    orderedParcels.reserve(candidates.size());
    for (const auto &c : nearRoads) orderedParcels.push_back(c.idx);
    for (const auto &c : interior) orderedParcels.push_back(c.idx);

    auto imprintFacility = [&](Building &b, Facility::Type type) {
        b.facility = true;
        b.facilityType = type;
        double area = std::max(b.footprint.width() * b.footprint.height(), 1.0);
        double scale = std::sqrt(area);
        if (type == Facility::Type::Hospital) {
            int target = static_cast<int>(std::round(4.0 + scale * 0.25));
            b.height = std::clamp(target, 5, 12);
        } else {
            int target = static_cast<int>(std::round(2.0 + scale * 0.1));
            b.height = std::clamp(target, 2, 5);
        }
    };

    auto placeFacilities = [&](Facility::Type type, std::uint32_t count) {
        std::uint32_t placed = 0;
        for (std::size_t idx : orderedParcels) {
            if (placed >= count) break;
            Building &b = city.buildings[idx];
            if (!b.facility) {
                imprintFacility(b, type);
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
