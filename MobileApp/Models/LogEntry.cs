using System.Text.Json.Serialization;
using Microsoft.Maui.Graphics;

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

    /// <summary>Icon emoji corespunzător tipului (design: ⚡ ⚠ ⏱).</summary>
    public string IconText => Type switch
    {
        "relay_change"     => "⚡",
        "sensor_err"       => "⚠",
        "override_expired" => "⏱",
        _                  => "📝"
    };

    /// <summary>Icon emoji corespunzător tipului.</summary>
    public string Icon => IconText;

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

    /// <summary>Culoare accent per tip eveniment.</summary>
    public Color AccentColor => Type switch
    {
        "relay_change"     => Color.FromArgb("#00e6ff"),
        "sensor_err"       => Color.FromArgb("#ff6644"),
        "override_expired" => Color.FromArgb("#ffaa44"),
        _                  => Color.FromArgb("#888888")
    };

    /// <summary>Culoare background translucidă (10% accent) pentru rândul de log.</summary>
    public Color RowBackgroundColor => Type switch
    {
        "relay_change"     => Color.FromArgb("#1A00e6ff"),
        "sensor_err"       => Color.FromArgb("#1Aff6644"),
        "override_expired" => Color.FromArgb("#1Affaa44"),
        _                  => Color.FromArgb("#0AFFFFFF")
    };

    /// <summary>Timestamp formatat pentru afișare locală (dd.MM HH:mm:ss).</summary>
    public string TsDisplay
    {
        get
        {
            if (string.IsNullOrEmpty(Ts)) return "—";
            if (DateTime.TryParse(Ts, out var dt))
                return dt.ToLocalTime().ToString("dd.MM HH:mm:ss");
            return Ts;
        }
    }
}

public class LogResponse
{
    [JsonPropertyName("entries")]
    public List<LogEntry> Entries { get; set; } = new();
}
