using System.Reflection;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using SkiaSharp.Views.Maui.Controls.Hosting;
using CommunityToolkit.Maui;
using UraniumUI;
using LibVLCSharp.Shared;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

// LibVLCSharp.MAUI 3.9.7.1 suporta nativ net10.0-android36.0 — nu mai avem nevoie de MediaElement fallback.

namespace ProiectVentilatie.Mobile;

public static class MauiProgram
{
    public static MauiApp CreateMauiApp()
    {
        var builder = MauiApp.CreateBuilder();
        builder
            .UseMauiApp<App>()
            .UseSkiaSharp()
            .UseMauiCommunityToolkit()
            .UseUraniumUI()
            .ConfigureFonts(fonts =>
            {
                fonts.AddFont("OpenSans-Regular.ttf", "OpenSansRegular");
                fonts.AddFont("OpenSans-Semibold.ttf", "OpenSansSemibold");
                fonts.AddFont("Rajdhani-SemiBold.ttf", "Rajdhani");
                fonts.AddFont("Rajdhani-Bold.ttf", "RajdhaniBold");
                fonts.AddFont("ShareTechMono-Regular.ttf", "ShareTechMono");
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

            builder.Services.Configure<MqttSettings>(config.GetSection("Mqtt"));
        }

        // LibVLC 3.9.7.1 suporta nativ net10.0-android36.0
        // Core.Initialize() poate fi oprita; fabricile LibVLC le creaza lazy per-tile
        try
        {
            Core.Initialize();
            Console.WriteLine("[Player] LibVLC Core initializat cu succes");
        }
        catch (Exception ex)
        {
            // Non-fatal — LibVLC se mai poate initializa per-tile pe Android
            Console.WriteLine($"[Player] LibVLC Core.Initialize avertisment: {ex.Message}");
        }
        builder.Services.AddSingleton<IPlayerFactory, LibVLCPlayerFactory>();

        // Services — MQTT
        builder.Services.AddSingleton<IMqttService, MqttService>();

        // Services — Camera
        builder.Services.AddSingleton<ICredentialStore, CredentialStore>();
        builder.Services.AddSingleton<ICameraConfigRepository, CameraConfigRepository>();
        builder.Services.AddSingleton<IImouCloudService, ImouCloudService>();
        builder.Services.AddSingleton<ICameraService, CameraService>();
        builder.Services.AddTransient<INetworkProbeService, NetworkProbeService>();

        // ViewModels
        builder.Services.AddSingleton<ViewModels.DashboardViewModel>();   // Singleton: evita re-connect la fiecare tab switch
        builder.Services.AddSingleton<ViewModels.SettingsViewModel>();    // Singleton: evita re-incarcare Preferences
        builder.Services.AddSingleton<ViewModels.SystemViewModel>();      // Singleton: evita re-subscribe MQTT
        builder.Services.AddSingleton<ViewModels.TvViewModel>();          // Singleton: evita re-subscribe MQTT
        builder.Services.AddSingleton<ViewModels.CamerasViewModel>();     // Singleton: evita re-attach VideoView
        builder.Services.AddTransient<ViewModels.CameraFullscreenViewModel>();
        builder.Services.AddTransient<ViewModels.CameraSettingsViewModel>();

        // Views
        builder.Services.AddSingleton<Views.DashboardPage>();             // Singleton: evita XAML re-inflate + lag
        builder.Services.AddSingleton<Views.SettingsPage>();              // Singleton: evita XAML re-inflate + lag
        builder.Services.AddSingleton<Views.SystemPage>();                // Singleton: evita XAML re-inflate + lag
        builder.Services.AddSingleton<Views.TvPage>();                    // Singleton: evita XAML re-inflate + lag
        builder.Services.AddSingleton<Views.CamerasPage>();               // Singleton: pereche cu VM
        builder.Services.AddTransient<Views.CameraFullscreenPage>();
        builder.Services.AddTransient<Views.CameraSettingsPage>();

        return builder.Build();
    }
}
