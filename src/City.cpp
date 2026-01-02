#include "City.h"

#include <fstream>
#include <sstream>
#include <iomanip>

City::City(int s) : size(s) {
    buildings.resize(size * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            Building &b = buildings[y * size + x];
            b.x = x;
            b.y = y;
            b.zone = ZoneType::None;
            b.height = 0;
            b.facility = false;
        }
    }
}

void City::saveOBJ(const std::string &filename) const {
    std::ofstream ofs(filename);
    if (!ofs) return;
    // Accumulate vertices and faces.  We write one object per building for
    // clarity, but the file can contain thousands of objects.  A running
    // vertex index is maintained to offset face indices.
    std::size_t vertexOffset = 1;
    for (const auto &b : buildings) {
        // Skip undeveloped and green cells; only extrude actual buildings
        if (b.zone == ZoneType::None || b.zone == ZoneType::Green) continue;
        // Determine extrusion height (floors).  Ensure at least one unit
        double h = static_cast<double>(b.height);
        if (h <= 0.0) h = 1.0;
        // Base (ground level) vertices
        double x0 = static_cast<double>(b.x);
        double y0 = static_cast<double>(b.y);
        double x1 = x0 + 1.0;
        double y1 = y0 + 1.0;
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
    for (const auto &b : buildings) {
        if (b.zone == ZoneType::None) { countUndeveloped++; continue; }
        if (b.zone == ZoneType::Residential) countResidential++;
        else if (b.zone == ZoneType::Commercial) countCommercial++;
        else if (b.zone == ZoneType::Industrial) countIndustrial++;
        else if (b.zone == ZoneType::Green) countGreen++;
        if (b.zone != ZoneType::Green) totalBuildings++;
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
