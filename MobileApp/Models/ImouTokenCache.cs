namespace ProiectVentilatie.Mobile.Models;

public class ImouTokenCache
{
    public string AccessToken      { get; set; } = string.Empty;
    public DateTime IssuedAt       { get; set; }
    public DateTime ExpiresAt      { get; set; }
    public long ServerTimeOffset   { get; set; } // seconds (localTime + offset = serverTime)

    public bool IsValid =>
        !string.IsNullOrEmpty(AccessToken) && DateTime.UtcNow < ExpiresAt;
}
