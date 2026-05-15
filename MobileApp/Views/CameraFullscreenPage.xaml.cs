using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.ViewModels;

namespace ProiectVentilatie.Mobile.Views;

[QueryProperty(nameof(CameraConfig), "CameraConfig")]
public partial class CameraFullscreenPage : ContentPage
{
    private readonly CameraFullscreenViewModel _vm;
    private IDispatcherTimer? _diskTimer;

    public CameraConfig? CameraConfig
    {
        set
        {
            if (value != null)
                _ = _vm.OpenAsync(value);
        }
    }

    public CameraFullscreenPage(CameraFullscreenViewModel vm)
    {
        InitializeComponent();
        _vm = vm;
        BindingContext = vm;
    }

    protected override void OnAppearing()
    {
        base.OnAppearing();

        // Forteaza landscape
        DeviceDisplay.Current.KeepScreenOn = true;

        // Check disk la fiecare 10s cand inregistreaza
        _diskTimer = Dispatcher.CreateTimer();
        _diskTimer.Interval = TimeSpan.FromSeconds(10);
        _diskTimer.Tick += (_, _) => _vm.CheckDiskSpace();
        _diskTimer.Start();
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
        _diskTimer?.Stop();
        DeviceDisplay.Current.KeepScreenOn = false;
        _vm.Close();
    }

    private async void OnBackTapped(object? sender, TappedEventArgs e)
    {
        await Shell.Current.GoToAsync("..");
    }
}
