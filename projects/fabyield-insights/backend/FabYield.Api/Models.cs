namespace FabYield.Api;

public sealed record Metadata(string Source, int RowsAccepted, int RowsRejected, int NotesRejected, int SchemaVersion);
public sealed record YieldSummary(int TotalLots, int TotalWafers, long DiesTested, long DiesPassed,
                                   long DiesFailed, double OverallYieldPct, int AlertCount);
public sealed record LotMetric(string Id, int Wafers, long DiesTested, long DiesPassed,
                               long DiesFailed, double YieldPct, double AvgCycleSeconds,
                               double AvgTemperatureC, string ToolId, string ProcessStep);
public sealed record ToolMetric(string Id, int Wafers, long DiesTested, long DiesPassed,
                                long DiesFailed, double YieldPct, double AvgCycleSeconds, double AvgTemperatureC);
public sealed record TrendPoint(string Date, int Wafers, long DiesTested, long DiesPassed,
                                long DiesFailed, double YieldPct, double AvgCycleSeconds, double AvgTemperatureC);
public sealed record YieldAlert(string LotId, string ToolId, string Severity, string Metric,
                                double Observed, double Threshold, string Reason);
public sealed record TechnicianNote(string LotId, string Message, bool Flagged);
public sealed record InvalidRow(int Line, string Reason);
public sealed record Snapshot(Metadata Meta, YieldSummary Summary, LotMetric[] Lots,
                              ToolMetric[] Tools, TrendPoint[] Trend, YieldAlert[] Alerts,
                              TechnicianNote[] Notes, InvalidRow[] InvalidRows);
