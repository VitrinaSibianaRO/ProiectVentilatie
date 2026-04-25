using System.Text.Json.Serialization;

namespace ProiectVentilatie.Mobile.Models;

public class VentilationState
{
    [JsonPropertyName("left")]
    public ZoneState Left { get; set; } = new();

    [JsonPropertyName("right")]
    public ZoneState Right { get; set; } = new();

    [JsonPropertyName("config")]
    public ConfigState Config { get; set; } = new();
}

public class ZoneState
{
    [JsonPropertyName("temp")]
    public float Temp { get; set; }

    [JsonPropertyName("hum")]
    public float Hum { get; set; }

    [JsonPropertyName("relay")]
    public bool Relay { get; set; }

    [JsonPropertyName("err")]
    public int Err { get; set; }
}

public class ConfigState
{
    [JsonPropertyName("threshT")]
    public float ThreshT { get; set; }

    [JsonPropertyName("threshH")]
    public float ThreshH { get; set; }

    [JsonPropertyName("marginT")]
    public float MarginT { get; set; }

    [JsonPropertyName("marginH")]
    public float MarginH { get; set; }

    [JsonPropertyName("interval")]
    public long Interval { get; set; }
}
