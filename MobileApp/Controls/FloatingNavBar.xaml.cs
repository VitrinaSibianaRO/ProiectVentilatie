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
        NavigateCommand = new Command<string>(async (page) => await NavigateToPage(page));
    }

    private async Task NavigateToPage(string page)
    {
        if (CurrentPage == page) return;
        
        // MAUI Shell navigation
        await Shell.Current.GoToAsync($"///{page}Page");
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
