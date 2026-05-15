using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.ViewModels;

namespace ProiectVentilatie.Mobile.Views;

public partial class CameraSettingsPage : ContentPage
{
    private readonly CameraSettingsViewModel _vm;

    public CameraSettingsPage(CameraSettingsViewModel vm)
    {
        InitializeComponent();
        _vm = vm;
        BindingContext = vm;
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        await _vm.LoadAsync();
    }

    private async void OnBackTapped(object? sender, TappedEventArgs e)
    {
        await Shell.Current.GoToAsync("..");
    }

    private async void OnEditCameraTapped(object? sender, TappedEventArgs e)
    {
        if (e.Parameter is not CameraConfig camera) return;
        await Shell.Current.DisplayAlertAsync("Edit", $"Edit {camera.Name} (TODO: popup)", "OK");
    }

    private async void OnDeleteCameraTapped(object? sender, TappedEventArgs e)
    {
        if (e.Parameter is not CameraConfig camera) return;
        var ok = await Shell.Current.DisplayAlertAsync("Sterge camera",
            $"Stergi '{camera.Name}'?", "Sterge", "Anuleaza");
        if (ok)
            _vm.DeleteCameraCommand.Execute(camera);
    }
}
