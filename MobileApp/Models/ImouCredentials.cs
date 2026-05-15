namespace ProiectVentilatie.Mobile.Models;

public class ImouCredentials
{
    public string AppId     { get; set; } = string.Empty;
    public string AppSecret { get; set; } = string.Empty;
    public string Region    { get; set; } = "eu"; // eu / us / ap
}
