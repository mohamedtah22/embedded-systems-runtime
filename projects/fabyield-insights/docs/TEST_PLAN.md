# Verification plan

## Claims and evidence

1. **Data integrity:** every accepted event has nonempty IDs, time-zone-qualified timestamp, valid die counts, and bounded telemetry values. Invalid inputs produce quarantined records without blocking good ones.
2. **Math correctness:** weighted die yield is the ratio of aggregated passed to tested dies. The unit test uses deliberately unequal denominators.
3. **Idempotent deterministic demo:** repeated generation at the same seed, number of lots, and wafers reproduces byte-identical output. Duplicate event IDs in a single input are excluded.
4. **Failure behavior:** an input with zero valid rows exits nonzero; snapshot publication is an atomic replace, not a partially flushed rewrite.
5. **API boundary:** read-only .NET endpoints expose quality metadata, validate filter/page inputs, and fail with HTTP 503 when analytics are missing or malformed. The HTTP smoke script runs against an actual .NET process.
6. **Delivery:** GitHub Actions builds Python pipeline, C# backend, React frontend, starts a 3-container Docker Compose stack, and requests a live API route.

## Limitations

- `yieldPct < 93` is a **demonstration threshold**, not a calibrated SPC limit or an Intel factory specification.
- Technician note flags are **keyword heuristics**, not natural-language diagnosis or verified root causes.
- The generated manufacturing data intentionally includes one low-performing equipment ID; it must never be confused with a real failure or actual Intel equipment.
- No security authentication, authorization, tenant isolation, auditing, PII handling, real cloud environment, production database, schema migrations, or Kafka stream processing.
- CSV duplicate tracking uses an in-memory set; for billions of events use a database key / partitioned deduplication.
- Kubernetes deployment examples are not tested against a cluster by the CI runner.

## Manual validation checklist

- [ ] `docker compose up --build -d` launches the three services without errors.
- [ ] Health returns 200 and a ready snapshot; HTML is served at port 8088.
- [ ] Lot filter and search show only matching results and alerts are visible.
- [ ] `GET /api/lots?page=0` returns 400.
- [ ] Remove snapshot; health returns 503 and UI explains that the API is unavailable.
- [ ] Run full CI on GitHub; inspect logs and record the actual outcome.
