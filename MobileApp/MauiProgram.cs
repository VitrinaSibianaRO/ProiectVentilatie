using System.Reflection;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using SkiaSharp.Views.Maui.Controls.Hosting;
using CommunityToolkit.Maui;
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
        builder.Services.AddTransient<ViewModels.DashboardViewModel>();
        builder.Services.AddTransient<ViewModels.SettingsViewModel>();
        builder.Services.AddTransient<ViewModels.SystemViewModel>();
        builder.Services.AddSingleton<ViewModels.CamerasViewModel>();      // Singleton: evita re-attach VideoView
        builder.Services.AddTransient<ViewModels.CameraFullscreenViewModel>();
        builder.Services.AddTransient<ViewModels.CameraSettingsViewModel>();

        // Views
        builder.Services.AddTransient<Views.DashboardPage>();
        builder.Services.AddTransient<Views.SettingsPage>();
        builder.Services.AddTransient<Views.SystemPage>();
        builder.Services.AddSingleton<Views.CamerasPage>();               // Singleton: pereche cu VM
        builder.Services.AddTransient<Views.CameraFullscreenPage>();
        builder.Services.AddTransient<Views.CameraSettingsPage>();

        return builder.Build();
    }
}
