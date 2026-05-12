namespace ProiectVentilatie.Mobile.Controls;

public partial class FloatingNavBar : ContentView
{
    public static readonly BindableProperty CurrentPageProperty = BindableProperty.Create(
        nameof(CurrentPage), typeof(string), typeof(FloatingNavBar), "Dashboard",
        propertyChanged: (b, o, n) => ((FloatingNavBar)b).UpdateVisualState());

    public string CurrentPage
    {
        get => (string)GetValue(CurrentPageProperty);
        set => SetValue(CurrentPageProperty, value);
    }

    public bool IsDashboardActive => CurrentPage == "Dashboard";
    public bool IsDevicesActive   => CurrentPage == "Devices";
    public bool IsSettingsActive  => CurrentPage == "Settings";
    public bool IsSystemActive    => CurrentPage == "System";

    public FloatingNavBar()
    {
        InitializeComponent();
        Console.WriteLine("[FloatingNavBar] CTOR fired");
    }

    private void OnTapDashboard(object? sender, TappedEventArgs e) => _ = NavigateToPage("Dashboard");
    private void OnTapDevices  (object? sender, TappedEventArgs e) => _ = NavigateToPage("Devices");
    private void OnTapSettings (object? sender, TappedEventArgs e) => _ = NavigateToPage("Settings");
    private void OnTapSystem   (object? sender, TappedEventArgs e) => _ = NavigateToPage("System");

    private async Task NavigateToPage(string page)
    {
        Console.WriteLine($"[FloatingNavBar] TAP page={page} current={CurrentPage}");

        if (CurrentPage == page)
        {
            Console.WriteLine($"[FloatingNavBar] Already on {page}, skip.");
            return;
        }

        var route = $"//{page}Page";
        try
        {
            await Shell.Current.GoToAsync(route, animate: false);
            Console.WriteLine($"[FloatingNavBar] Navigated OK to {route}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[FloatingNavBar] Navigation error to {route}: {ex.Message}");
        }
    }

    private void UpdateVisualState()
    {
        OnPropertyChanged(nameof(IsDashboardActive));
        OnPropertyChanged(nameof(IsDevicesActive));
        OnPropertyChanged(nameof(IsSettingsActive));
        OnPropertyChanged(nameof(IsSystemActive));
    }
}
