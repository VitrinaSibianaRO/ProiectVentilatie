using System.Globalization;

namespace ProiectVentilatie.Mobile.Converters;

/// <summary>
/// Converts bool → Color.
/// ConverterParameter: "green" (default) | "orange" | "red"
/// true  → PrimaryGreen / PrimaryOrange / PrimaryRed
/// false → dimmed grey
/// </summary>
public class BoolToColorConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        bool isTrue = value is bool b && b;
        string mode = parameter as string ?? "green";

        return mode switch
        {
            "orange" => isTrue ? Color.FromArgb("#ff7a45") : Color.FromArgb("#66FFFFFF"),
            "red"    => isTrue ? Color.FromArgb("#ff4422") : Color.FromArgb("#66FFFFFF"),
            "cyan"   => isTrue ? Color.FromArgb("#00e6ff") : Color.FromArgb("#66FFFFFF"),
            _        => isTrue ? Color.FromArgb("#00e87a") : Color.FromArgb("#66FFFFFF"),
        };
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotImplementedException();
}
