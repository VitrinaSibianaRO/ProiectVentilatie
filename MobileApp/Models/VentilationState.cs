using System.Text.Json.Serialization;

namespace ProiectVentilatie.Mobile.Models;

/// <summary>
/// Model aliniat cu JSON-ul publicat de ESP32 pe ventilatie/state.
/// Câmpuri: left, right, config, lock, fw, uptimeSec, heap.
/// </summary>
public class VentilationState
{
    [JsonPropertyName("left")]
    public ZoneState Left { get; set; } = new();

    [JsonPropertyName("right")]
    public ZoneState Right { get; set; } = new();

    [JsonPropertyName("config")]
    public ConfigState Config { get; set; } = new();

    [JsonPropertyName("lock")]
    public LockState? Lock { get; set; }

    [JsonPropertyName("fw")]
    public int Fw { get; set; }

    [JsonPropertyName("uptimeSec")]
    public long UptimeSec { get; set; }

    [JsonPropertyName("heap")]
    public int Heap { get; set; }

    [JsonPropertyName("led")]
    public LedState? Led { get; set; }
}

public class ZoneState
{
    [JsonPropertyName("temp")]
    public float Temp { get; set; }

    [JsonPropertyName("hum")]
    public float Hum { get; set; }

    [JsonPropertyName("relay")]
    public bool Relay { get; set; }

    [JsonPropertyName("override")]
    public bool Override { get; set; }

    [JsonPropertyName("errs")]
    public int Errs { get; set; }

    [JsonPropertyName("failsafe")]
    public bool Failsafe { get; set; }
}

public class ConfigState
{
    [JsonPropertyName("threshT")]
    public float ThreshT { get; set; }

    [JsonPropertyName("threshH")]
    public float ThreshH { get; set; }

    [JsonPropertyName("interval")]
    public int Interval { get; set; }

    [JsonPropertyName("hystT")]
    public float HystT { get; set; } = 2.0f;

    [JsonPropertyName("hystH")]
    public float HystH { get; set; } = 5.0f;
}

public class LockState
{
    [JsonPropertyName("owner")]
    public string? Owner { get; set; }

    [JsonPropertyName("ageMs")]
    public int AgeMs { get; set; }
}

public class LedState
{
    [JsonPropertyName("intensity")]
    public int Intensity { get; set; }

    [JsonPropertyName("schedEnabled")]
    public bool SchedEnabled { get; set; }

    [JsonPropertyName("followTv")]
    public bool FollowTv { get; set; }

    [JsonPropertyName("morseText")]
    public string MorseText { get; set; } = "SUGI PULA";
}
