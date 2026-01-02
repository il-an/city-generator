import Vapor

private enum GeneratorError: Error {
    case processFailed(status: Int32, stderr: String)
    case missingArtifacts(String)
}

extension GeneratorError: AbortError {
    var status: HTTPResponseStatus { .internalServerError }

    var reason: String {
        switch self {
        case let .processFailed(status, stderr):
            return "City generation failed (status: \(status))\n\(stderr)"
        case let .missingArtifacts(id):
            return "Expected output missing for \(id)"
        }
    }
}

final class GeneratorService {
    private let generatorScript: String
    private let outputDirectory: URL
    private let fileManager: FileManager

    init(generatorScript: String, outputDirectory: URL, fileManager: FileManager = .default) {
        self.generatorScript = generatorScript
        self.outputDirectory = outputDirectory
        self.fileManager = fileManager
    }

    func ensureOutputDirectory() throws {
        if !fileManager.fileExists(atPath: outputDirectory.path) {
            try fileManager.createDirectory(at: outputDirectory, withIntermediateDirectories: true)
        }
    }

    func generate(request: GenerationRequest, on eventLoop: EventLoop) -> EventLoopFuture<GeneratedCity> {
        return eventLoop.makeFutureWithTask {
            try await self.runGenerator(request: request)
        }
    }

    func listCities(on eventLoop: EventLoop) -> EventLoopFuture<[GeneratedCity]> {
        eventLoop.makeFutureWithTask {
            try self.availableCities()
        }
    }

    func summaryPath(for id: String, on eventLoop: EventLoop) -> EventLoopFuture<URL> {
        eventLoop.makeFutureWithTask {
            let cleaned = self.cleanIdentifier(id)
            let summary = self.outputDirectory
                .appendingPathComponent(cleaned, isDirectory: true)
                .appendingPathComponent("city_summary.json")
            guard self.fileManager.fileExists(atPath: summary.path) else {
                throw Abort(.notFound, reason: "Summary not found for \(cleaned)")
            }
            return summary
        }
    }

    func modelPath(for id: String, on eventLoop: EventLoop) -> EventLoopFuture<URL> {
        eventLoop.makeFutureWithTask {
            let cleaned = self.cleanIdentifier(id)
            let model = self.outputDirectory
                .appendingPathComponent(cleaned, isDirectory: true)
                .appendingPathComponent("city.obj")
            guard self.fileManager.fileExists(atPath: model.path) else {
                throw Abort(.notFound, reason: "Model not found for \(cleaned)")
            }
            return model
        }
    }

    private func runGenerator(request: GenerationRequest) async throws -> GeneratedCity {
        try ensureOutputDirectory()

        let identifier = cleanIdentifier(request.output ?? Self.defaultIdentifier())
        let outputPath = outputDirectory.appendingPathComponent(identifier, isDirectory: true)

        if !fileManager.fileExists(atPath: outputPath.path) {
            try fileManager.createDirectory(at: outputPath, withIntermediateDirectories: true)
        }

        let arguments = buildArguments(for: request, outputPath: outputPath.path)
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        process.arguments = ["python3", generatorScript] + arguments

        let stdout = Pipe()
        let stderr = Pipe()
        process.standardOutput = stdout
        process.standardError = stderr

        try process.run()
        process.waitUntilExit()

        let errorData = stderr.fileHandleForReading.readDataToEndOfFile()
        let errorText = String(decoding: errorData, as: UTF8.self)

        guard process.terminationStatus == 0 else {
            throw GeneratorError.processFailed(status: process.terminationStatus, stderr: errorText)
        }

        let summaryURL = outputPath.appendingPathComponent("city_summary.json")
        let modelURL = outputPath.appendingPathComponent("city.obj")

        guard fileManager.fileExists(atPath: summaryURL.path),
              fileManager.fileExists(atPath: modelURL.path) else {
            throw GeneratorError.missingArtifacts(identifier)
        }

        let summary = try JSONDecoder().decode(CitySummary.self, from: Data(contentsOf: summaryURL))
        return GeneratedCity(id: identifier, summary: summary, summaryPath: summaryURL, modelPath: modelURL)
    }

    private func availableCities() throws -> [GeneratedCity] {
        guard let contents = try? fileManager.contentsOfDirectory(at: outputDirectory, includingPropertiesForKeys: nil, options: .skipsHiddenFiles) else {
            return []
        }

        return contents.compactMap { dir -> GeneratedCity? in
            guard dir.hasDirectoryPath else { return nil }
            let summary = dir.appendingPathComponent("city_summary.json")
            let model = dir.appendingPathComponent("city.obj")
            guard fileManager.fileExists(atPath: summary.path),
                  fileManager.fileExists(atPath: model.path),
                  let data = try? Data(contentsOf: summary),
                  let decoded = try? JSONDecoder().decode(CitySummary.self, from: data) else {
                return nil
            }
            return GeneratedCity(id: dir.lastPathComponent, summary: decoded, summaryPath: summary, modelPath: model)
        }.sorted { $0.id < $1.id }
    }

    private func buildArguments(for request: GenerationRequest, outputPath: String) -> [String] {
        var args: [String] = []

        func append<T>(_ value: T?, flag: String) { if let value = value { args.append("--\(flag)=\(value)") } }

        append(request.population, flag: "population")
        append(request.hospitals, flag: "hospitals")
        append(request.schools, flag: "schools")
        append(request.transport, flag: "transport")
        append(request.seed, flag: "seed")
        append(request.gridSize, flag: "grid-size")
        append(request.radiusFraction, flag: "radius-fraction")
        args.append("--output=\(outputPath)")

        return args
    }

    private static func defaultIdentifier() -> String {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        let dateString = formatter.string(from: Date())
        return "city_\(dateString.replacingOccurrences(of: ":", with: "-"))"
    }

    private func cleanIdentifier(_ raw: String) -> String {
        let allowed = CharacterSet.alphanumerics.union(CharacterSet(charactersIn: "-_"))
        let filtered = raw.unicodeScalars.map { allowed.contains($0) ? Character($0) : "-" }
        let cleaned = String(filtered).trimmingCharacters(in: CharacterSet(charactersIn: "-"))
        return cleaned.isEmpty ? Self.defaultIdentifier() : cleaned
    }
}

private struct GeneratorServiceKey: StorageKey { typealias Value = GeneratorService }

extension Application {
    var generatorService: GeneratorService {
        get {
            guard let service = storage[GeneratorServiceKey.self] else {
                fatalError("GeneratorService not configured")
            }
            return service
        }
        set { storage[GeneratorServiceKey.self] = newValue }
    }
}
