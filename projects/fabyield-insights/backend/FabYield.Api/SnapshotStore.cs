using System.Text.Json;

namespace FabYield.Api;

/// <summary>Read immutable, atomically replaced analytics snapshots without holding locks on writes.</summary>
public sealed class SnapshotStore
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private readonly string _path;

    public SnapshotStore(IConfiguration configuration)
    {
        _path = configuration["SNAPSHOT_PATH"] ?? "/data/snapshot.json";
    }

    public Snapshot? Read()
    {
        if (!File.Exists(_path)) return null;
        try
        {
            using var stream = File.OpenRead(_path);
            var snapshot = JsonSerializer.Deserialize<Snapshot>(stream, JsonOptions);
            if (snapshot is null || snapshot.Meta.SchemaVersion != 1 || snapshot.Summary.DiesTested < 1)
                return null;
            return snapshot;
        }
        catch (IOException) { return null; }
        catch (JsonException) { return null; }
    }
}
