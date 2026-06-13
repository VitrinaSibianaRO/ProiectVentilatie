using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

public partial class CameraFullscreenViewModel : ObservableObject, IDisposable
{
    private readonly IPlayerFactory _factory;
    private readonly ICameraService _cameraService;
    private IVideoPlayerHandle? _player;

    public CameraConfig? Config { get; private set; }

    [ObservableProperty] private View? _playerView;
    [ObservableProperty] private bool _isBuffering;
    [ObservableProperty] private bool _isRecording;
    [ObservableProperty] private bool _isAudioMuted;
    [ObservableProperty] private NetworkScope _activeScope;
    [ObservableProperty] private string? _lastError;
    [ObservableProperty] private string _cameraName = string.Empty;
    [ObservableProperty] private long _diskFreeBytes;
    [ObservableProperty] private bool _diskWarning;

    private static readonly long WarnThreshold    = 500L * 1024 * 1024; // 500 MB
    private static readonly long AutoStopThreshold = 100L * 1024 * 1024; // 100 MB

    public CameraFullscreenViewModel(IPlayerFactory factory, ICameraService cameraService)
    {
        _factory       = factory;
        _cameraService = cameraService;
    }

    public async Task OpenAsync(CameraConfig config)
    {
        Config = config;
        CameraName = config.Name;
        IsBuffering = true;
        LastError = null;

        _player?.Dispose();
        _player = _factory.Create();
        _player.IsMuted = false; // fullscreen: audio activ by default
        IsAudioMuted = false;

        _player.PlaybackStarted += (_, _) =>
            MainThread.BeginInvokeOnMainThread(() => IsBuffering = false);
        _player.ErrorOccurred += (_, msg) =>
            MainThread.BeginInvokeOnMainThread(() => { LastError = msg; IsBuffering = false; });

        PlayerView = _player.PlayerView;

        try
        {
            var (uri, scope) = await _cameraService.ResolveStreamUriAsync(config, fullscreen: true);
            ActiveScope = scope;
            _player.SetSource(uri);
            _player.Play();
        }
        catch (Exception ex)
        {
            LastError = ex.Message;
            IsBuffering = false;
        }
    }

    [RelayCommand]
    private void ToggleMute()
    {
        IsAudioMuted = !IsAudioMuted;
        if (_player != null) _player.IsMuted = IsAudioMuted;
    }

    [RelayCommand]
    private async Task TakeSnapshotAsync()
    {
        if (_player == null || Config == null) return;
        var dir = Path.Combine(FileSystem.AppDataDirectory, "Snapshots");
        Directory.CreateDirectory(dir);
        var path = Path.Combine(dir, $"cam_{Config.Id:N}_{DateTime.Now:yyyyMMdd_HHmmss}.jpg");
        var ok = await _player.TakeSnapshotAsync(path);
        if (ok)
            await Shell.Current.DisplayAlertAsync("Snapshot", $"Salvat: {Path.GetFileName(path)}", "OK");
        else
            await Shell.Current.DisplayAlertAsync("Eroare", LastError ?? "Snapshot esuat", "OK");
    }

    [RelayCommand]
    private void ToggleRecord()
    {
        if (IsRecording)
        {
            _player?.StopRecording();
            IsRecording = false;
        }
        else
        {
            if (Config == null) return;
            CheckDiskSpace();
            if (DiskFreeBytes < AutoStopThreshold) return;

            var dir = Path.Combine(FileSystem.AppDataDirectory, "Recordings");
            Directory.CreateDirectory(dir);
            var path = Path.Combine(dir, $"cam_{Config.Id:N}_{DateTime.Now:yyyyMMdd_HHmmss}.mp4");
            var ok = _player?.StartRecording(path) ?? false;
            if (ok) IsRecording = true;
            else LastError = "Recording nu a putut porni";
        }
    }

    public void CheckDiskSpace()
    {
        try
        {
            var info = new DriveInfo(FileSystem.AppDataDirectory[..1]);
            DiskFreeBytes = info.AvailableFreeSpace;
            DiskWarning = DiskFreeBytes < WarnThreshold;

            if (DiskFreeBytes < AutoStopThreshold && IsRecording)
            {
                _player?.StopRecording();
                IsRecording = false;
                LastError = "Recording oprit — spatiu liber insuficient (<100 MB)";
            }
        }
        catch { /* ignoram erori DriveInfo */ }
    }

    public void Close()
    {
        _player?.Stop();
        IsRecording = false;
    }

    public void Dispose()
    {
        Close();
        _player?.Dispose();
        _player = null;
    }
}
