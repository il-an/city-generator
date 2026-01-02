#include "City.h"

#include <fstream>
#include <array>
#include <cmath>

namespace {

// Write a rectangular prism defined by four base corners to an OBJ stream.
// The corners should be specified in winding order around the base face.
void writePrism(std::ofstream &ofs,
                const std::array<std::pair<double, double>, 4> &base,
                double baseZ,
                double topZ,
                std::size_t &vertexOffset) {
    ofs << "v " << base[0].first << " " << base[0].second << " " << baseZ << "\n";
    ofs << "v " << base[1].first << " " << base[1].second << " " << baseZ << "\n";
    ofs << "v " << base[2].first << " " << base[2].second << " " << baseZ << "\n";
    ofs << "v " << base[3].first << " " << base[3].second << " " << baseZ << "\n";
    ofs << "v " << base[0].first << " " << base[0].second << " " << topZ << "\n";
    ofs << "v " << base[1].first << " " << base[1].second << " " << topZ << "\n";
    ofs << "v " << base[2].first << " " << base[2].second << " " << topZ << "\n";
    ofs << "v " << base[3].first << " " << base[3].second << " " << topZ << "\n";
    auto v = vertexOffset;
    ofs << "f " << v << " " << v + 1 << " " << v + 2 << "\n";
    ofs << "f " << v << " " << v + 2 << " " << v + 3 << "\n";
    ofs << "f " << v + 4 << " " << v + 7 << " " << v + 6 << "\n";
    ofs << "f " << v + 4 << " " << v + 6 << " " << v + 5 << "\n";
    ofs << "f " << v << " " << v + 4 << " " << v + 5 << "\n";
    ofs << "f " << v << " " << v + 5 << " " << v + 1 << "\n";
    ofs << "f " << v + 1 << " " << v + 5 << " " << v + 6 << "\n";
    ofs << "f " << v + 1 << " " << v + 6 << " " << v + 2 << "\n";
    ofs << "f " << v + 2 << " " << v + 6 << " " << v + 7 << "\n";
    ofs << "f " << v + 2 << " " << v + 7 << " " << v + 3 << "\n";
    ofs << "f " << v + 3 << " " << v + 7 << " " << v + 4 << "\n";
    ofs << "f " << v + 3 << " " << v + 4 << " " << v << "\n";
    vertexOffset += 8;
}

constexpr double kRoadThickness = 0.05;

} // namespace

City::City(int s) : size(s) {
    zones.resize(size * size, ZoneType::None);
}

void City::saveOBJ(const std::string &filename) const {
    std::ofstream ofs(filename);
    if (!ofs) return;
    // Accumulate vertices and faces.  We write one object per parcel-based
    // building for clarity, but the file can contain thousands of objects.
    // A running vertex index is maintained to offset face indices.
    std::size_t vertexOffset = 1;
    for (const auto &b : buildings) {
        // Skip undeveloped and green cells; only extrude actual buildings
        if (b.zone == ZoneType::None || b.zone == ZoneType::Green) continue;
        // Determine extrusion height (floors).  Ensure at least one unit
        double h = static_cast<double>(b.height);
        if (h <= 0.0) h = 1.0;
        // Base (ground level) vertices
        double x0 = b.footprint.x0;
        double y0 = b.footprint.y0;
        double x1 = b.footprint.x1;
        double y1 = b.footprint.y1;
        double z0 = 0.0;
        double z1 = h;
        // Write vertices
        ofs << "v " << x0 << " " << y0 << " " << z0 << "\n";
        ofs << "v " << x1 << " " << y0 << " " << z0 << "\n";
        ofs << "v " << x1 << " " << y1 << " " << z0 << "\n";
        ofs << "v " << x0 << " " << y1 << " " << z0 << "\n";
        ofs << "v " << x0 << " " << y0 << " " << z1 << "\n";
        ofs << "v " << x1 << " " << y0 << " " << z1 << "\n";
        ofs << "v " << x1 << " " << y1 << " " << z1 << "\n";
        ofs << "v " << x0 << " " << y1 << " " << z1 << "\n";
        // Each face is two triangles.  Indices are relative to current object.
        auto v = vertexOffset;
        // bottom face (v0,v1,v2,v3)
        ofs << "f " << v << " " << v + 1 << " " << v + 2 << "\n";
        ofs << "f " << v << " " << v + 2 << " " << v + 3 << "\n";
        // top face (v4,v5,v6,v7)
        ofs << "f " << v + 4 << " " << v + 7 << " " << v + 6 << "\n";
        ofs << "f " << v + 4 << " " << v + 6 << " " << v + 5 << "\n";
        // sides
        // front (v0,v1,v5,v4)
        ofs << "f " << v << " " << v + 4 << " " << v + 5 << "\n";
        ofs << "f " << v << " " << v + 5 << " " << v + 1 << "\n";
        // right (v1,v2,v6,v5)
        ofs << "f " << v + 1 << " " << v + 5 << " " << v + 6 << "\n";
        ofs << "f " << v + 1 << " " << v + 6 << " " << v + 2 << "\n";
        // back (v2,v3,v7,v6)
        ofs << "f " << v + 2 << " " << v + 6 << " " << v + 7 << "\n";
        ofs << "f " << v + 2 << " " << v + 7 << " " << v + 3 << "\n";
        // left (v3,v0,v4,v7)
        ofs << "f " << v + 3 << " " << v + 7 << " " << v + 4 << "\n";
        ofs << "f " << v + 3 << " " << v + 4 << " " << v << "\n";
        // Update vertexOffset
        vertexOffset += 8;
    }
    // Roads: extrude each centreline into a thin rectangular prism so that
    // the street hierarchy is visible in the 3D export.
    for (const auto &road : roads) {
        double dx = road.x2 - road.x1;
        double dy = road.y2 - road.y1;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) continue;
        double invLen = 1.0 / len;
        double nx = -dy * invLen;
        double ny = dx * invLen;
        double halfWidth = 0.5 * roadWidth(road.type);
        double hx = nx * halfWidth;
        double hy = ny * halfWidth;
        std::array<std::pair<double, double>, 4> base = {{
            {road.x1 + hx, road.y1 + hy},
            {road.x1 - hx, road.y1 - hy},
            {road.x2 - hx, road.y2 - hy},
            {road.x2 + hx, road.y2 + hy}
        }};
        writePrism(ofs, base, 0.0, kRoadThickness, vertexOffset);
    }
    ofs.close();
}

void City::saveSummary(const std::string &filename) const {
    std::ofstream ofs(filename);
    if (!ofs) return;
    // Count metrics
    std::size_t countResidential = 0;
    std::size_t countCommercial = 0;
    std::size_t countIndustrial = 0;
    std::size_t countGreen = 0;
    std::size_t countUndeveloped = 0;
    std::size_t totalBuildings = 0;
    for (const auto z : zones) {
        if (z == ZoneType::None) { countUndeveloped++; continue; }
        if (z == ZoneType::Residential) countResidential++;
        else if (z == ZoneType::Commercial) countCommercial++;
        else if (z == ZoneType::Industrial) countIndustrial++;
        else if (z == ZoneType::Green) countGreen++;
    }
    for (const auto &b : buildings) {
        if (b.zone != ZoneType::None && b.zone != ZoneType::Green) {
            totalBuildings++;
        }
    }
    std::size_t countHospitals = 0;
    std::size_t countSchools = 0;
    for (const auto &f : facilities) {
        if (f.type == Facility::Type::Hospital) countHospitals++;
        else if (f.type == Facility::Type::School) countSchools++;
    }
    // Write JSON.  Note: this is simplistic and not pretty‑printed.
    ofs << "{\n";
    ofs << "  \"gridSize\": " << size << ",\n";
    ofs << "  \"totalBuildings\": " << totalBuildings << ",\n";
    ofs << "  \"residentialCells\": " << countResidential << ",\n";
    ofs << "  \"commercialCells\": " << countCommercial << ",\n";
    ofs << "  \"industrialCells\": " << countIndustrial << ",\n";
    ofs << "  \"greenCells\": " << countGreen << ",\n";
    ofs << "  \"undevelopedCells\": " << countUndeveloped << ",\n";
    ofs << "  \"numHospitals\": " << countHospitals << ",\n";
    ofs << "  \"numSchools\": " << countSchools << "\n";
    ofs << "}";
    ofs.close();
}
