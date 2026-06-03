using ProiectVentilatie.Mobile.Services;
using ProiectVentilatie.Mobile.ViewModels;

namespace ProiectVentilatie.Mobile;

public partial class App : Application
{
    private readonly IMqttService _mqtt;
    private readonly CamerasViewModel _cameras;
    private readonly IServiceProvider _services;

    public App(IMqttService mqttService, CamerasViewModel camerasViewModel, IServiceProvider services)
    {
        InitializeComponent();
        _mqtt     = mqttService;
        _cameras  = camerasViewModel;
        _services = services;
    }

    protected override Window CreateWindow(IActivationState? activationState)
    {
        return new Window(new AppShell(_services));
    }

    protected override void OnSleep()
    {
        base.OnSleep();
        _ = _mqtt.DisconnectAsync();
        _cameras.StopAllStreams();
    }

    protected override void OnResume()
    {
        base.OnResume();
        _ = _mqtt.ConnectAsync();
        _cameras.RestartVisibleStreams();
    }
}
