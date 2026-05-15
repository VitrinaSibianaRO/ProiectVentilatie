using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

/// <summary>
/// Singleton VM pentru CamerasPage — evita re-attach VideoView la tab-switch.
/// </summary>
public partial class CamerasViewModel : ObservableObject, IDisposable
{
    private readonly ICameraConfigRepository _repo;
    private readonly IPlayerFactory _playerFactory;
    private readonly ICameraService _cameraService;

    public ObservableCollection<StreamCellViewModel> Cameras { get; } = new();

    [ObservableProperty] private int _gridSpan = 1;
    [ObservableProperty] private bool _isLoading;
    [ObservableProperty] private bool _hasNoCameras;
    [ObservableProperty] private string? _errorMessage;

    private double _availableWidth;

    public CamerasViewModel(
        ICameraConfigRepository repo,
        IPlayerFactory playerFactory,
        ICameraService cameraService)
    {
        _repo          = repo;
        _playerFactory = playerFactory;
        _cameraService = cameraService;
    }

    [RelayCommand]
    public async Task LoadCamerasAsync()
    {
        IsLoading = true;
        ErrorMessage = null;

        try
        {
            var configs = await _repo.GetAllAsync();
            var enabled = configs.Where(c => c.IsEnabled).OrderBy(c => c.DisplayOrder).ToList();

            // Dispune VM-uri pentru camere sterse
            var toRemove = Cameras
                .Where(vm => !enabled.Any(c => c.Id == vm.Config.Id))
                .ToList();
            foreach (var vm in toRemove) { Cameras.Remove(vm); vm.Dispose(); }

            // Adauga VM-uri noi pentru camere noi
            foreach (var config in enabled)
            {
                if (!Cameras.Any(vm => vm.Config.Id == config.Id))
                    Cameras.Add(new StreamCellViewModel(config, _playerFactory, _cameraService));
            }

            HasNoCameras = Cameras.Count == 0;
        }
        catch (Exception ex)
        {
            ErrorMessage = ex.Message;
        }
        finally { IsLoading = false; }
    }

    public void UpdateGridSpan(double availableWidth)
    {
        _availableWidth = availableWidth;
        GridSpan = Math.Max(1, (int)(availableWidth / 320));
    }

    /// <summary>Apelat de Page la scroll — actualizeaza IsActive pe tile-urile vizibile.</summary>
    public void UpdateVisibleTiles(IReadOnlyList<int> visibleIndices)
    {
        for (int i = 0; i < Cameras.Count; i++)
        {
            var isVisible = visibleIndices.Contains(i);
            if (Cameras[i].IsActive != isVisible)
                Cameras[i].IsActive = isVisible;
        }
    }

    /// <summary>Apelat din App.OnSleep — opreste toate streamurile.</summary>
    public void StopAllStreams()
    {
        foreach (var vm in Cameras) vm.StopAll();
    }

    /// <summary>Apelat din App.OnResume — reporneste tile-urile vizibile anterior.</summary>
    public void RestartVisibleStreams()
    {
        foreach (var vm in Cameras.Where(vm => vm.IsActive))
            _ = vm.ActivateAsync();
    }

    public void Dispose()
    {
        foreach (var vm in Cameras) vm.Dispose();
        Cameras.Clear();
    }
}
