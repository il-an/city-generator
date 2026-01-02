#include "City.h"

#include <fstream>
#include <array>
#include <cmath>
#include <algorithm>

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

// Convenience helper to extrude an axis-aligned rectangle into a prism.
void writeRectPrism(std::ofstream &ofs, const Rect &r,
                    double baseZ, double topZ, std::size_t &vertexOffset) {
    std::array<std::pair<double, double>, 4> base = {{
        {r.x0, r.y0},
        {r.x1, r.y0},
        {r.x1, r.y1},
        {r.x0, r.y1}
    }};
    writePrism(ofs, base, baseZ, topZ, vertexOffset);
}

// Inset a rectangle by a fixed amount, clamping so the rectangle never flips.
Rect insetRect(const Rect &r, double inset) {
    Rect out = r;
    double maxInset = std::min(r.width(), r.height()) * 0.49;
    double applied = std::clamp(inset, 0.0, maxInset);
    out.x0 += applied;
    out.x1 -= applied;
    out.y0 += applied;
    out.y1 -= applied;
    return out;
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
    auto emitStandard = [&](const Building &b) {
        double h = std::max(1.0, static_cast<double>(b.height));
        writeRectPrism(ofs, b.footprint, 0.0, h, vertexOffset);
    };
    auto emitPark = [&](const Rect &fp) {
        double margin = std::min(fp.width(), fp.height()) * 0.08;
        Rect lawn = insetRect(fp, margin);
        double padHeight = 0.08;
        writeRectPrism(ofs, lawn, 0.0, padHeight, vertexOffset);
        double baseSize = std::min(lawn.width(), lawn.height()) * 0.2;
        double planterSize = std::clamp(baseSize, 0.2, std::min(lawn.width(), lawn.height()) * 0.45);
        Rect planterA{lawn.x0, lawn.y0, lawn.x0 + planterSize, lawn.y0 + planterSize};
        Rect planterB{lawn.x1 - planterSize, lawn.y1 - planterSize, lawn.x1, lawn.y1};
        double planterHeight = padHeight * 2.5;
        writeRectPrism(ofs, planterA, padHeight, padHeight + planterHeight, vertexOffset);
        writeRectPrism(ofs, planterB, padHeight, padHeight + planterHeight, vertexOffset);
    };
    auto emitSchool = [&](const Building &b) {
        const Rect &fp = b.footprint;
        double w = fp.width();
        double h = fp.height();
        Rect field = insetRect(fp, std::min(w, h) * 0.07);
        double fieldHeight = 0.05;
        writeRectPrism(ofs, field, 0.0, fieldHeight, vertexOffset);
        bool wide = w >= h;
        double buildingW = wide ? w * 0.45 : w * 0.6;
        double buildingH = wide ? h * 0.6 : h * 0.45;
        Rect buildingRect;
        buildingRect.x0 = fp.x0 + w * 0.08;
        buildingRect.y0 = fp.y0 + h * (wide ? 0.2 : 0.08);
        buildingRect.x1 = buildingRect.x0 + buildingW;
        buildingRect.y1 = buildingRect.y0 + buildingH;
        double maxX = fp.x1 - w * 0.05;
        double maxY = fp.y1 - h * 0.05;
        if (buildingRect.x1 > maxX) {
            double shift = buildingRect.x1 - maxX;
            buildingRect.x0 -= shift;
            buildingRect.x1 -= shift;
        }
        if (buildingRect.y1 > maxY) {
            double shift = buildingRect.y1 - maxY;
            buildingRect.y0 -= shift;
            buildingRect.y1 -= shift;
        }
        double schoolHeight = std::max(2.0, static_cast<double>(b.height));
        writeRectPrism(ofs, buildingRect, 0.0, schoolHeight, vertexOffset);
    };
    auto emitHospital = [&](const Building &b) {
        const Rect &fp = b.footprint;
        double w = fp.width();
        double h = fp.height();
        Rect podium = insetRect(fp, std::min(w, h) * 0.08);
        double podiumTop = std::max(1.2, static_cast<double>(b.height) * 0.25);
        writeRectPrism(ofs, podium, 0.0, podiumTop, vertexOffset);
        double cx = fp.centreX();
        double cy = fp.centreY();
        bool wide = w >= h;
        double mainW = wide ? w * 0.7 : w * 0.45;
        double mainH = wide ? h * 0.45 : h * 0.7;
        Rect main{cx - mainW * 0.5, cy - mainH * 0.5, cx + mainW * 0.5, cy + mainH * 0.5};
        double mainTop = std::max(podiumTop + 2.0, static_cast<double>(b.height));
        writeRectPrism(ofs, main, podiumTop, mainTop, vertexOffset);
        double wingW = wide ? w * 0.28 : w * 0.85;
        double wingH = wide ? h * 0.85 : h * 0.28;
        Rect wing{cx - wingW * 0.5, cy - wingH * 0.5, cx + wingW * 0.5, cy + wingH * 0.5};
        double wingTop = std::max(podiumTop + 1.2, mainTop * 0.9);
        writeRectPrism(ofs, wing, podiumTop, wingTop, vertexOffset);
    };
    for (const auto &b : buildings) {
        if (b.zone == ZoneType::None) continue;
        if (b.zone == ZoneType::Green) {
            emitPark(b.footprint);
            continue;
        }
        if (b.facility) {
            if (b.facilityType == Facility::Type::Hospital) {
                emitHospital(b);
            } else {
                emitSchool(b);
            }
            continue;
        }
        emitStandard(b);
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
