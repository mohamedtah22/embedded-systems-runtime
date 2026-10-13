# Design decisions

**Backend:** C# ASP.NET Core 8 Minimal APIs provide type-safe contracts for the aggregated results and a read-only query surface. `SnapshotStore` handles missing or malformed inputs; the API never claims production-grade security or database semantics.

**Data processing:** Python `csv.DictReader` streams telemetry, with explicit validity rules and row quarantine. Summary values are weighted by individual die counts. JSONL technician notes demonstrate ingesting a second, unstructured source; their extracted flags are merely hints.

**Frontend:** React/TypeScript separates loading/error state from transformed API results. SVG trends, searchable and filterable lot tables, metrics and tool comparisons are constructed without charting service dependencies. The UI visibly discloses data as synthetic.

**Containers:** Python ETL container publishes to a named volume; API mounts it read-only; Nginx builds and serves the React site, proxying same-origin `/api` calls. The exposed Docker port binds **only** to `127.0.0.1:8088`.

**Deployment note:** included k8s Pod/Service manifest uses an init container to create test data in an ephemeral shared volume; replace image references with images built and pushed to your own registry before applying. This does not demonstrate actual Kubernetes experience until the deployment has been verified.
