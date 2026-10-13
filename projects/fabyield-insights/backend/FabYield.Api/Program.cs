using FabYield.Api;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddSingleton<SnapshotStore>();
var app = builder.Build();

app.MapGet("/api/health", (SnapshotStore store) =>
    store.Read() is not null
        ? Results.Ok(new { status = "ok", snapshot = "ready" })
        : Results.Json(new { status = "unavailable", snapshot = "missing-or-invalid" }, statusCode: 503));

app.MapGet("/api/summary", (SnapshotStore store) =>
    WithSnapshot(store, data => Results.Ok(new { data.Summary, data.Meta })));

app.MapGet("/api/lots", (SnapshotStore store, string? tool, double? minYield,
                         int? page, int? pageSize) =>
{
    if ((page ?? 1) < 1 || (pageSize ?? 20) is < 1 or > 100 ||
        (minYield.HasValue && (minYield.Value < 0 || minYield.Value > 100)))
        return Results.BadRequest(new { error = "invalid pagination or yield filter" });
    return WithSnapshot(store, data =>
    {
        IEnumerable<LotMetric> query = data.Lots;
        if (!string.IsNullOrWhiteSpace(tool))
            query = query.Where(x => x.ToolId.Equals(tool, StringComparison.OrdinalIgnoreCase));
        if (minYield.HasValue)
            query = query.Where(x => x.YieldPct >= minYield.Value);
        var filtered = query.OrderBy(x => x.YieldPct).ThenBy(x => x.Id).ToArray();
        var current = page ?? 1;
        var size = pageSize ?? 20;
        return Results.Ok(new { total = filtered.Length, page = current, pageSize = size,
                                items = filtered.Skip((current - 1) * size).Take(size) });
    });
});

app.MapGet("/api/tools", (SnapshotStore store) =>
    WithSnapshot(store, data => Results.Ok(data.Tools)));

app.MapGet("/api/trend", (SnapshotStore store) =>
    WithSnapshot(store, data => Results.Ok(data.Trend)));

app.MapGet("/api/alerts", (SnapshotStore store, string? severity) =>
{
    if (severity is not null && severity is not ("high" or "medium"))
        return Results.BadRequest(new { error = "severity must be high or medium" });
    return WithSnapshot(store, data => Results.Ok(
        data.Alerts.Where(a => severity is null || a.Severity == severity).ToArray()));
});

app.MapGet("/api/notes", (SnapshotStore store, bool? flagged) =>
    WithSnapshot(store, data => Results.Ok(
        data.Notes.Where(n => !flagged.HasValue || n.Flagged == flagged.Value).ToArray())));

app.MapGet("/api/data-quality", (SnapshotStore store) =>
    WithSnapshot(store, data => Results.Ok(new { data.Meta, rejected = data.InvalidRows.Take(100) })));

app.Run();

static IResult WithSnapshot(SnapshotStore store, Func<Snapshot, IResult> success)
{
    var snapshot = store.Read();
    return snapshot is null
        ? Results.Problem(title: "Analytics unavailable", detail: "Run the data pipeline first.",
                          statusCode: 503)
        : success(snapshot);
}
