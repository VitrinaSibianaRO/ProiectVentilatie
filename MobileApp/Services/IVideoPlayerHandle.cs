namespace ProiectVentilatie.Mobile.Services;

/// <summary>
/// Abstractizare player video — implementata de LibVLCPlayerHandle sau MediaElementPlayerHandle.
/// Creat de IPlayerFactory; detinut de StreamCellViewModel.
/// PlayerView e View-ul MAUI care trebuie pus in layout.
/// </summary>
public interface IVideoPlayerHandle : IDisposable
{
    View PlayerView { get; }

    void SetSource(Uri uri);
    void Play();
    void Stop();
    void Pause();

    bool IsMuted { get; set; }
    bool IsPlaying { get; }
    bool IsRecording { get; }

    Task<bool> TakeSnapshotAsync(string outputPath);
    bool StartRecording(string outputPath);
    void StopRecording();

    event EventHandler? PlaybackStarted;
    event EventHandler<string>? ErrorOccurred;
}
