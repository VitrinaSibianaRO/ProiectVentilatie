using CommunityToolkit.Mvvm.ComponentModel;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

/// <summary>
/// VM per tile camera in grid. Lazy: playerul e creat la IsActive=true, dispus la false.
/// </summary>
public partial class StreamCellViewModel : ObservableObject, IDisposable
{
    private readonly IPlayerFactory _factory;
    private readonly ICameraService _cameraService;
    private IVideoPlayerHandle? _player;
    private bool _disposed;

    public CameraConfig Config { get; }

    [ObservableProperty] private bool _isActive;
    [ObservableProperty] private bool _isBuffering;
    [ObservableProperty] private bool _isAudioMuted = true;
    [ObservableProperty] private bool _isRecording;
    [ObservableProperty] private NetworkScope _activeScope = NetworkScope.Cloud;
    [ObservableProperty] private string? _lastError;
    [ObservableProperty] private View? _playerView;

    public string Name => Config.Name;
    public bool IsEnabled => Config.IsEnabled;

    public StreamCellViewModel(CameraConfig config, IPlayerFactory factory, ICameraService cameraService)
    {
        Config = config;
        _factory = factory;
        _cameraService = cameraService;
    }

    partial void OnIsActiveChanged(bool value)
    {
        if (value) _ = ActivateAsync();
        else Deactivate();
    }

    partial void OnIsAudioMutedChanged(bool value)
    {
        if (_player != null) _player.IsMuted = value;
    }

    public async Task ActivateAsync()
    {
        if (_disposed || _player != null) return;

        IsBuffering = true;
        LastError = null;

        try
        {
            _player = _factory.Create();
            _player.IsMuted = IsAudioMuted;
            _player.PlaybackStarted += (_, _) => MainThread.BeginInvokeOnMainThread(() => IsBuffering = false);
            _player.ErrorOccurred   += (_, msg) => MainThread.BeginInvokeOnMainThread(() =>
            {
                LastError = msg;
                IsBuffering = false;
            });

            PlayerView = _player.PlayerView;

            var (uri, scope) = await _cameraService.ResolveStreamUriAsync(Config);
            ActiveScope = scope;
            _player.SetSource(uri);
            _player.Play();
        }
        catch (Exception ex)
        {
            LastError = ex.Message;
            IsBuffering = false;
            Deactivate();
        }
    }

    public void Deactivate()
    {
        if (_player == null) return;
        _player.Stop();
        _player.Dispose();
        _player = null;
        MainThread.BeginInvokeOnMainThread(() => PlayerView = null);
        IsBuffering = false;
        IsRecording = false;
    }

    public void StopAll()
    {
        _player?.Stop();
        IsRecording = false;
    }

    public async Task<bool> TakeSnapshotAsync(string outputPath)
    {
        if (_player == null) return false;
        return await _player.TakeSnapshotAsync(outputPath);
    }

    public bool StartRecording(string outputPath)
    {
        if (_player == null) return false;
        var ok = _player.StartRecording(outputPath);
        if (ok) IsRecording = true;
        return ok;
    }

    public void StopRecording()
    {
        _player?.StopRecording();
        IsRecording = false;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Deactivate();
    }
}
