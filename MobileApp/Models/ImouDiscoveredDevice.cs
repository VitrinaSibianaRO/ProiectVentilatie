namespace ProiectVentilatie.Mobile.Models;

public class ImouDiscoveredDevice
{
    public string DeviceId   { get; set; } = string.Empty;
    public string Name       { get; set; } = string.Empty;
    public string LocalIp    { get; set; } = string.Empty;
    public bool   IsOnline   { get; set; }
    public string Model      { get; set; } = string.Empty;
    public string Firmware   { get; set; } = string.Empty;
    public List<int> Channels { get; set; } = new() { 1 };
}
