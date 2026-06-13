using System.Text.Json.Serialization;

namespace ProiectVentilatie.Mobile.Models;

public class CameraConfig
{
    [JsonPropertyName("id")]
    public Guid Id { get; set; } = Guid.NewGuid();

    [JsonPropertyName("name")]
    public string Name { get; set; } = string.Empty;

    [JsonPropertyName("displayOrder")]
    public int DisplayOrder { get; set; }

    [JsonPropertyName("imouDeviceId")]
    public string ImouDeviceId { get; set; } = string.Empty;

    [JsonPropertyName("imouChannelId")]
    public int ImouChannelId { get; set; } = 1;

    [JsonPropertyName("localIp")]
    public string LocalIp { get; set; } = string.Empty;

    [JsonPropertyName("rtspPort")]
    public int RtspPort { get; set; } = 554;

    [JsonPropertyName("rtspUsername")]
    public string RtspUsername { get; set; } = "admin";

    [JsonPropertyName("preferredScope")]
    public NetworkScope PreferredScope { get; set; } = NetworkScope.Auto;

    [JsonPropertyName("isEnabled")]
    public bool IsEnabled { get; set; } = true;

    [JsonPropertyName("createdAt")]
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;

    [JsonPropertyName("updatedAt")]
    public DateTime UpdatedAt { get; set; } = DateTime.UtcNow;
}
