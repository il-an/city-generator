# City Generator Vapor Server

This Vapor app exposes the procedural city generator over HTTP. It wraps the existing `python/generate_city.py` script and serves the resulting OBJ mesh and JSON summary files.

## Running

```bash
cd vapor-server
swift run
```

By default the server looks for the Python wrapper one directory up (`../python/generate_city.py`) and writes generated artefacts under `../generated_cities`. Override these paths with environment variables:

- `CITYGEN_SCRIPT` – path to `generate_city.py`
- `CITYGEN_OUTPUT_DIR` – output directory for generated runs

## Endpoints

- `GET /health` – liveness probe
- `POST /generate` – generate a new city. Supply any of the generator options in the JSON body (e.g. `population`, `gridSize`, `radiusFraction`, `output`).
- `GET /cities` – list generated cities with links to their artefacts
- `GET /cities/{id}/summary` – download `city_summary.json`
- `GET /cities/{id}/model` – download `city.obj`

Outputs are identified by their output directory name. If no `output` is provided in the request, the server creates a unique timestamped identifier.
