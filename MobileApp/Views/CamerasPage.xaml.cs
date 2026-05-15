using ProiectVentilatie.Mobile.ViewModels;

namespace ProiectVentilatie.Mobile.Views;

public partial class CamerasPage : ContentPage
{
    private readonly CamerasViewModel _vm;
    private bool _firstLoad = true;

    // Tracked from scroll events
    private double _scrollOffsetY;
    private DateTime _lastScrollUpdate = DateTime.MinValue;
    private static readonly TimeSpan ScrollThrottle = TimeSpan.FromMilliseconds(200);

    public CamerasPage(CamerasViewModel vm)
    {
        InitializeComponent();
        _vm = vm;
        BindingContext = vm;
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        _vm.UpdateGridSpan(Width > 0 ? Width : DeviceDisplay.MainDisplayInfo.Width / DeviceDisplay.MainDisplayInfo.Density);

        if (_firstLoad)
        {
            _firstLoad = false;
            await _vm.LoadCamerasAsync();
            UpdateVisibleTiles();
        }
    }

    protected override void OnSizeAllocated(double width, double height)
    {
        base.OnSizeAllocated(width, height);
        if (width > 0)
            _vm.UpdateGridSpan(width);
    }

    private void OnCollectionScrolled(object? sender, ItemsViewScrolledEventArgs e)
    {
        _scrollOffsetY = e.VerticalOffset;
        if (DateTime.UtcNow - _lastScrollUpdate < ScrollThrottle) return;
        _lastScrollUpdate = DateTime.UtcNow;
        UpdateVisibleTiles();
    }

    private void UpdateVisibleTiles()
    {
        var totalCams = _vm.Cameras.Count;
        if (totalCams == 0) return;

        var span        = _vm.GridSpan;
        var tileH       = Width > 0 ? (Width / span) * (9.0 / 16.0) + 28 : 200; // 16:9 + label row
        var pageH       = Height > 0 ? Height : 800;
        var viewportTop = _scrollOffsetY;
        var viewportBot = _scrollOffsetY + pageH;

        var visible = new List<int>();
        for (int i = 0; i < totalCams; i++)
        {
            var row   = i / span;
            var tileT = row * tileH;
            var tileB = tileT + tileH;
            if (tileB >= viewportTop - 100 && tileT <= viewportBot + 100)
                visible.Add(i);
        }

        _vm.UpdateVisibleTiles(visible);
    }

    private async void OnTileTapped(object? sender, TappedEventArgs e)
    {
        if (sender is not Grid grid) return;
        if (grid.BindingContext is not StreamCellViewModel cellVm) return;

        await Shell.Current.GoToAsync(nameof(CameraFullscreenPage),
            new Dictionary<string, object> { ["CameraConfig"] = cellVm.Config });
    }

    private async void OnOpenSettingsTapped(object? sender, TappedEventArgs e)
    {
        await Shell.Current.GoToAsync(nameof(CameraSettingsPage));
    }
}
