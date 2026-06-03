using ProiectVentilatie.Mobile.ViewModels;

namespace ProiectVentilatie.Mobile.Views;

public partial class TvPage : ContentPage
{
    private TvViewModel? _vm;

    public TvPage(TvViewModel viewModel)
    {
        InitializeComponent();
        BindingContext = _vm = viewModel;
    }

    private void OnVolumeDragCompleted(object? sender, EventArgs e)
    {
        System.Diagnostics.Debug.WriteLine($"[TvPage] VolumeDragCompleted vol={_vm?.TvVolume}");
        _ = _vm?.SetVolumeCommand.ExecuteAsync(null);
    }

    private void OnMuteToggled(object? sender, ToggledEventArgs e)
    {
        System.Diagnostics.Debug.WriteLine($"[TvPage] MuteToggled -> {e.Value}");
        _ = _vm?.ToggleMuteCommand.ExecuteAsync(null);
    }

    private void OnBacklightDragCompleted(object? sender, EventArgs e)
    {
        System.Diagnostics.Debug.WriteLine($"[TvPage] BacklightDragCompleted bk={_vm?.TvBacklight}");
        _ = _vm?.SetBacklightCommand.ExecuteAsync(null);
    }

    private void OnNoSignalOffToggled(object? sender, ToggledEventArgs e)
    {
        System.Diagnostics.Debug.WriteLine($"[TvPage] NoSignalOff -> {e.Value}");
        _ = _vm?.SetNoSignalPowerOffCommand.ExecuteAsync(null);
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
    }
}
