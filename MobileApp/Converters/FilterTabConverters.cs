using System.Globalization;

namespace ProiectVentilatie.Mobile.Converters;

/// <summary>
/// Returns background Color for a filter tab button.
/// Value = currently selected filter string, Parameter = this tab's label.
/// Active:   #1A00e6ff (translucent cyan)
/// Inactive: #08FFFFFF (subtle white)
/// </summary>
public class FilterTabBgConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        bool active = value is string s && parameter is string p && s == p;
        return active ? Color.FromArgb("#1A00e6ff") : Color.FromArgb("#08FFFFFF");
    }
    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotImplementedException();
}

/// <summary>
/// Returns stroke Color for a filter tab button (active = cyan, inactive = subtle white).
/// </summary>
public class FilterTabStrokeConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        bool active = value is string s && parameter is string p && s == p;
        return active ? Color.FromArgb("#4400e6ff") : Color.FromArgb("#15FFFFFF");
    }
    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotImplementedException();
}

/// <summary>
/// Returns text Color for a filter tab button (active = cyan, inactive = dim white).
/// </summary>
public class FilterTabTextConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        bool active = value is string s && parameter is string p && s == p;
        return active ? Color.FromArgb("#00e6ff") : Color.FromArgb("#88FFFFFF");
    }
    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotImplementedException();
}
