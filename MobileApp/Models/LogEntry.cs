using System.Text.Json.Serialization;

namespace ProiectVentilatie.Mobile.Models;

/// <summary>
/// Element din log-ul de evenimente publicat de ESP32 pe ventilatie/log.
/// Format: {"entries":[{"ts":"2026-04-29T14:32:15Z","type":"sensor_err","zone":"left","msg":"5 erori"},...]}
/// </summary>
public class LogEntry
{
    [JsonPropertyName("ts")]
    public string? Ts { get; set; }

    [JsonPropertyName("type")]
    public string? Type { get; set; }

    [JsonPropertyName("zone")]
    public string? Zone { get; set; }

    [JsonPropertyName("msg")]
    public string? Msg { get; set; }

    // ── Display helpers (computate la deserializare) ──

    /// <summary>Icon emoji corespunzător tipului.</summary>
    public string Icon => Type switch
    {
        "sensor_err"       => "⚠️",
        "relay_change"     => "🔌",
        "override_expired" => "⏱️",
        _                  => "📝"
    };

    /// <summary>Etichetă tip lizibilă.</summary>
    public string TypeLabel => Type switch
    {
        "sensor_err"       => "Eroare senzor",
        "relay_change"     => "Schimbare releu",
        "override_expired" => "Override expirat",
        _                  => Type ?? "necunoscut"
    };

    /// <summary>Etichetă zonă lizibilă.</summary>
    public string ZoneLabel => Zone switch
    {
        "left"  => "Stânga",
        "right" => "Dreapta",
        _       => "—"
    };

    /// <summary>Timestamp formatat pentru afișare locală.</summary>
    public string TsDisplay
    {
        get
        {
            if (string.IsNullOrEmpty(Ts)) return "—";
            // Dacă e ISO 8601 → parse + local
            if (DateTime.TryParse(Ts, out var dt))
                return dt.ToLocalTime().ToString("dd MMM HH:mm:ss");
            // Fallback (uptime:NNNs)
            return Ts;
        }
    }
}

public class LogResponse
{
    [JsonPropertyName("entries")]
    public List<LogEntry> Entries { get; set; } = new();
}
