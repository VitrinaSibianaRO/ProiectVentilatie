using System.Globalization;
using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Converters;

// Converter pentru afisarea numelui prietenos al unui LedPattern in Picker
public class LedPatternToNameConverter : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is LedPattern p) return LedPatternInfo.DisplayName(p);
        return value?.ToString() ?? string.Empty;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => null;
}

// Usage: IsVisible="{Binding LedMode, Converter={StaticResource PatternIdVis}, ConverterParameter=1}"
public class PatternIdToVisibilityConverter : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is LedPattern pattern && int.TryParse(parameter as string, out int target))
            return (int)pattern == target;
        return false;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => null;
}

// Inversa: ascunde sectiunea cand e selectat un pattern specific
public class PatternIdToInvisibilityConverter : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is LedPattern pattern && int.TryParse(parameter as string, out int target))
            return (int)pattern != target;
        return true;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => null;
}
