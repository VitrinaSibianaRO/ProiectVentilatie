namespace ProiectVentilatie.Mobile;

public partial class AppShell : Shell
{
    public AppShell(IServiceProvider services)
    {
        InitializeComponent();

        // Setam Content direct din DI (singleton) in loc de ContentTemplate lazy.
        // GoToAsync va folosi instantele deja create — fara lag la prima navigare.
        DashboardTab.Content = services.GetRequiredService<Views.DashboardPage>();
        CamerasTab.Content   = services.GetRequiredService<Views.CamerasPage>();
        SettingsTab.Content  = services.GetRequiredService<Views.SettingsPage>();
        SystemTab.Content    = services.GetRequiredService<Views.SystemPage>();
        TvTab.Content        = services.GetRequiredService<Views.TvPage>();

        // Route-uri modale accesibile cu GoToAsync
        Routing.RegisterRoute(nameof(Views.CameraFullscreenPage), typeof(Views.CameraFullscreenPage));
        Routing.RegisterRoute(nameof(Views.CameraSettingsPage),   typeof(Views.CameraSettingsPage));
    }
}
