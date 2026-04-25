using Microsoft.Extensions.Logging;

namespace ProiectVentilatie.Mobile;

public static class MauiProgram
{
    public static MauiApp CreateMauiApp()
    {
        var builder = MauiApp.CreateBuilder();
        builder
            .UseMauiApp<App>()
            .ConfigureFonts(fonts =>
            {
                fonts.AddFont("OpenSans-Regular.ttf", "OpenSansRegular");
                fonts.AddFont("OpenSans-Semibold.ttf", "OpenSansSemibold");
            });

#if DEBUG
        builder.Logging.AddDebug();
#endif

        // Services
        builder.Services.AddSingleton<ProiectVentilatie.Mobile.Services.IMqttService, ProiectVentilatie.Mobile.Services.MqttService>();

        // ViewModels
        builder.Services.AddTransient<ProiectVentilatie.Mobile.ViewModels.DashboardViewModel>();

        // Views
        builder.Services.AddTransient<ProiectVentilatie.Mobile.Views.DashboardPage>();

        return builder.Build();
    }
}
