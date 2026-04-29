using System.Reflection;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using ProiectVentilatie.Mobile.Models;

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

        // Load embedded appsettings.json
        var assembly = Assembly.GetExecutingAssembly();
        using var stream = assembly.GetManifestResourceStream(
            "ProiectVentilatie.Mobile.appsettings.json");

        if (stream != null)
        {
            var config = new ConfigurationBuilder()
                .AddJsonStream(stream)
                .Build();

            // Bind Mqtt section → MqttSettings
            builder.Services.Configure<MqttSettings>(config.GetSection("Mqtt"));
        }

        // Services
        builder.Services.AddSingleton<Services.IMqttService, Services.MqttService>();

        // ViewModels
        builder.Services.AddTransient<ViewModels.DashboardViewModel>();
        builder.Services.AddTransient<ViewModels.SettingsViewModel>();
        builder.Services.AddTransient<ViewModels.SystemViewModel>();
        builder.Services.AddTransient<ViewModels.LogViewModel>();

        // Views
        builder.Services.AddTransient<Views.DashboardPage>();
        builder.Services.AddTransient<Views.SettingsPage>();
        builder.Services.AddTransient<Views.SystemPage>();
        builder.Services.AddTransient<Views.LogPage>();

        return builder.Build();
    }
}
