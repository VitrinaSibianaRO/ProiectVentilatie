using System.Globalization;
using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Converters;

public class NetworkScopeToColorConverter : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        return value is NetworkScope scope ? scope switch
        {
            NetworkScope.Lan   => Color.FromArgb("#00E6FF"), // cyan — LAN
            NetworkScope.Cloud => Color.FromArgb("#FF8C00"), // portocaliu — CLOUD
            _                  => Color.FromArgb("#FF3030")  // rosu — OFFLINE/Auto-failed
        } : Colors.Gray;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
