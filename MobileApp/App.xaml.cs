using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile;

public partial class App : Application
{
    private readonly IMqttService _mqtt;

    public App(IMqttService mqttService)
    {
        InitializeComponent();
        _mqtt = mqttService;
    }

    protected override Window CreateWindow(IActivationState? activationState)
    {
        return new Window(new AppShell());
    }

    protected override void OnSleep()
    {
        base.OnSleep();
        // Deconectare curată la background — economie baterie
        _ = _mqtt.DisconnectAsync();
    }

    protected override void OnResume()
    {
        base.OnResume();
        // Reconectare automată la foreground
        _ = _mqtt.ConnectAsync();
    }
}
