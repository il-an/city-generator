import Vapor

struct GenerationRequest: Content {
    var population: Int?
    var hospitals: Int?
    var schools: Int?
    var transport: String?
    var seed: Int?
    var gridSize: Int?
    var radiusFraction: Double?
    var output: String?
}

struct CitySummary: Content, Codable {
    let gridSize: Int
    let totalBuildings: Int
    let residentialCells: Int
    let commercialCells: Int
    let industrialCells: Int
    let greenCells: Int
    let undevelopedCells: Int
    let numHospitals: Int
    let numSchools: Int
    let maxDistanceToSchool: Double
    let maxDistanceToHospital: Double
    let maxResidentialHeight: Int
    let maxCommercialHeight: Int
    let maxIndustrialHeight: Int
}

struct GeneratedCity: Content {
    let id: String
    let summary: CitySummary
    let summaryPath: URL
    let modelPath: URL
}

struct GenerationResponse: Content {
    let id: String
    let summary: CitySummary
    let summaryURL: String
    let modelURL: String

    init(from generated: GeneratedCity, basePath: String = "") {
        id = generated.id
        summary = generated.summary

        let root = basePath.hasSuffix("/") ? String(basePath.dropLast()) : basePath
        let summaryPath = root + "/cities/\(generated.id)/summary"
        let modelPath = root + "/cities/\(generated.id)/model"

        summaryURL = summaryPath.isEmpty ? "/cities/\(generated.id)/summary" : summaryPath
        modelURL = modelPath.isEmpty ? "/cities/\(generated.id)/model" : modelPath
    }
}
