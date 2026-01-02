import Vapor

func routes(_ app: Application) throws {
    let controller = GenerationController(generator: app.generatorService)

    app.get("health") { _ in
        return "ok"
    }

    app.post("generate", use: controller.generate)
    app.get("cities", use: controller.listCities)
    app.get("cities", ":id", "summary", use: controller.getSummary)
    app.get("cities", ":id", "model", use: controller.getModel)
}
