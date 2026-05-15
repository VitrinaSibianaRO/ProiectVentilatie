namespace ProiectVentilatie.Mobile;

public partial class AppShell : Shell
{
    public AppShell()
    {
        InitializeComponent();

        // Route-uri modale accesibile cu GoToAsync
        Routing.RegisterRoute(nameof(Views.CameraFullscreenPage), typeof(Views.CameraFullscreenPage));
        Routing.RegisterRoute(nameof(Views.CameraSettingsPage),   typeof(Views.CameraSettingsPage));
    }
}
