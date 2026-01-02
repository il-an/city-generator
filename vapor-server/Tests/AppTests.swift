import XCTest
@testable import App

final class AppTests: XCTestCase {
    func testGenerationResponsePaths() {
        let summary = CitySummary(
            gridSize: 1,
            totalBuildings: 0,
            residentialCells: 0,
            commercialCells: 0,
            industrialCells: 0,
            greenCells: 0,
            undevelopedCells: 0,
            numHospitals: 0,
            numSchools: 0,
            maxDistanceToSchool: 0,
            maxDistanceToHospital: 0,
            maxResidentialHeight: 0,
            maxCommercialHeight: 0,
            maxIndustrialHeight: 0
        )
        let city = GeneratedCity(id: "demo", summary: summary, summaryPath: URL(fileURLWithPath: "/tmp/summary"), modelPath: URL(fileURLWithPath: "/tmp/model"))
        let response = GenerationResponse(from: city)
        XCTAssertEqual(response.summaryURL, "/cities/demo/summary")
        XCTAssertEqual(response.modelURL, "/cities/demo/model")
    }
}
