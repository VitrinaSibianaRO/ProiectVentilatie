using System.Text.Json.Serialization;

namespace ProiectVentilatie.Mobile.Models;

public class TvState
{
    [JsonPropertyName("power")]        public bool    Power           { get; set; }
    [JsonPropertyName("volume")]       public int     Volume          { get; set; }
    [JsonPropertyName("muted")]        public bool    Muted           { get; set; }
    [JsonPropertyName("inputId")]      public int     InputId         { get; set; }
    [JsonPropertyName("tempC")]        public int     TemperatureC    { get; set; }
    [JsonPropertyName("signal")]       public bool    HasSignal       { get; set; }
    [JsonPropertyName("hours")]        public long    UsageHours      { get; set; }
    [JsonPropertyName("backlight")]    public int     Backlight       { get; set; }
    [JsonPropertyName("pictureMode")]  public int     PictureMode     { get; set; }
    [JsonPropertyName("energySaving")] public int     EnergySaving    { get; set; }
    [JsonPropertyName("noSignalOff")]  public bool    NoSignalPowerOff { get; set; }
    [JsonPropertyName("serial")]       public string  Serial          { get; set; } = string.Empty;
    [JsonPropertyName("swVersion")]    public string  SwVersion       { get; set; } = string.Empty;
    [JsonPropertyName("reachable")]    public bool    Reachable       { get; set; }
}
