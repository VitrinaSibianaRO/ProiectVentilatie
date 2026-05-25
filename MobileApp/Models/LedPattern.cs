namespace ProiectVentilatie.Mobile.Models;

public enum LedPattern : byte
{
    Steady    = 0,
    Breathing = 1,
    Triangle  = 2,
    Sawtooth  = 3,
    Strobe    = 4,
    Heartbeat = 5,
    Candle    = 6,
    Lightning = 7,
    SosMorse  = 8,
    Sunrise   = 9,
    Sunset    = 10,
    Random    = 11
}

public static class LedPatternInfo
{
    public static string DisplayName(LedPattern p) => p switch
    {
        LedPattern.Steady    => "Constantă",
        LedPattern.Breathing => "Respirație (sinus)",
        LedPattern.Triangle  => "Triunghi",
        LedPattern.Sawtooth  => "Dinți de fierăstrău",
        LedPattern.Strobe    => "Strobe",
        LedPattern.Heartbeat => "Bătaie de inimă",
        LedPattern.Candle    => "Lumânare",
        LedPattern.Lightning => "Fulgere",
        LedPattern.SosMorse  => "SOS (Morse)",
        LedPattern.Sunrise   => "Răsărit",
        LedPattern.Sunset    => "Apus",
        LedPattern.Random    => "Aleator",
        _                    => p.ToString()
    };

    public static LedPattern[] All =>
        (LedPattern[])Enum.GetValues(typeof(LedPattern));
}
