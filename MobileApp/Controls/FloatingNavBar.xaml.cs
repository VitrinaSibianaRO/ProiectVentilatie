using System.Windows.Input;

namespace ProiectVentilatie.Mobile.Controls;

public partial class FloatingNavBar : ContentView
{
    public static readonly BindableProperty CurrentPageProperty = BindableProperty.Create(
        nameof(CurrentPage), typeof(string), typeof(FloatingNavBar), "Dashboard", propertyChanged: (b, o, n) => ((FloatingNavBar)b).UpdateVisualState());

    public string CurrentPage { get => (string)GetValue(CurrentPageProperty); set => SetValue(CurrentPageProperty, value); }

    public bool IsDashboardActive => CurrentPage == "Dashboard";
    public bool IsDevicesActive => CurrentPage == "Devices";
    public bool IsSettingsActive => CurrentPage == "Settings";
    public bool IsReportsActive => CurrentPage == "Reports";
    public bool IsSystemActive => CurrentPage == "System";

    public ICommand NavigateCommand { get; }

    public FloatingNavBar()
    {
        InitializeComponent();
        BindingContext = this;
        NavigateCommand = new Command<string>(async (page) => await NavigateToPage(page));
    }

    private async Task NavigateToPage(string page)
    {
        System.Diagnostics.Debug.WriteLine($"[FloatingNavBar] Navigating to: {page}");
        if (CurrentPage == page)
        {
            System.Diagnostics.Debug.WriteLine($"[FloatingNavBar] Already on {page}, ignoring.");
            return;
        }

        var route = page == "Reports" ? "//ReportsPage" : $"//{page}Page";
        try
        {
            await Shell.Current.GoToAsync(route);
            System.Diagnostics.Debug.WriteLine($"[FloatingNavBar] Navigated OK to {route}");
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[FloatingNavBar] Navigation error to {route}: {ex}");
        }
    }

    private void UpdateVisualState()
    {
        OnPropertyChanged(nameof(IsDashboardActive));
        OnPropertyChanged(nameof(IsDevicesActive));
        OnPropertyChanged(nameof(IsSettingsActive));
        OnPropertyChanged(nameof(IsReportsActive));
        OnPropertyChanged(nameof(IsSystemActive));
    }
}
