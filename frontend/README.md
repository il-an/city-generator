# City Generator Frontend

A small React interface for the Vapor API that drives the procedural city generator. Adjust parameters, trigger `/generate`, and browse/download generated artefacts from `/cities`.

## Running locally

```bash
cd frontend
npm install
npm run dev
```

By default the app talks to the API at the same origin (e.g. `http://localhost:8080`). Point it at a remote server by setting an environment variable when running Vite:

```bash
VITE_API_BASE_URL=http://localhost:8080 npm run dev
```

Build for production with:

```bash
npm run build
npm run preview
```
