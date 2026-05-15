using ProiectVentilatie.Mobile.Services;
using ProiectVentilatie.Mobile.ViewModels;

namespace ProiectVentilatie.Mobile;

public partial class App : Application
{
    private readonly IMqttService _mqtt;
    private readonly CamerasViewModel _cameras;

    public App(IMqttService mqttService, CamerasViewModel camerasViewModel)
    {
        InitializeComponent();
        _mqtt    = mqttService;
        _cameras = camerasViewModel;
    }

    protected override Window CreateWindow(IActivationState? activationState)
    {
        return new Window(new AppShell());
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
