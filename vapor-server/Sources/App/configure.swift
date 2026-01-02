import Vapor

public func configure(_ app: Application) throws {
    let repoRoot = URL(fileURLWithPath: app.directory.workingDirectory)
        .appendingPathComponent("..")
        .standardizedFileURL

    let defaultOutput = repoRoot.appendingPathComponent("generated_cities", isDirectory: true)
    let outputDirectory = URL(fileURLWithPath: Environment.get("CITYGEN_OUTPUT_DIR") ?? defaultOutput.path)

    let defaultScript = repoRoot.appendingPathComponent("python/generate_city.py")
    let generatorScript = Environment.get("CITYGEN_SCRIPT") ?? defaultScript.path

    app.generatorService = GeneratorService(
        generatorScript: generatorScript,
        outputDirectory: outputDirectory
    )

    try app.generatorService.ensureOutputDirectory()

    app.middleware.use(FileMiddleware(publicDirectory: app.directory.publicDirectory))

    try routes(app)
}
