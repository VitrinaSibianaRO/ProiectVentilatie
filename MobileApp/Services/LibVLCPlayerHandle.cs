using LibVLCSharp.Shared;
using LibVLCSharp.MAUI;

namespace ProiectVentilatie.Mobile.Services;

/// <summary>
/// Player handle bazat pe LibVLC — suporta RTSP LAN, HLS, snapshot si recording MP4.
/// Creat de LibVLCPlayerFactory; dispus de StreamCellViewModel la deactivare.
/// </summary>
public class LibVLCPlayerHandle : IVideoPlayerHandle
{
    private readonly LibVLC _libVLC;
    private readonly MediaPlayer _mediaPlayer;
    private readonly VideoView _videoView;
    private Media? _currentMedia;
    private Uri? _currentUri;
    private bool _isRecording;
    private bool _disposed;

    public View PlayerView => _videoView;
    public bool IsPlaying => _mediaPlayer.IsPlaying;
    public bool IsRecording => _isRecording;

    public event EventHandler? PlaybackStarted;
    public event EventHandler<string>? ErrorOccurred;

    public bool IsMuted
    {
        get => _mediaPlayer.Mute;
        set => _mediaPlayer.Mute = value;
    }

    public LibVLCPlayerHandle(LibVLC libVLC)
    {
        _libVLC = libVLC;
        _mediaPlayer = new MediaPlayer(libVLC) { Mute = true };
        _videoView = new VideoView();

        // Attach MediaPlayer la VideoView dupa ce view-ul e gata
        _videoView.MediaPlayerChanged += OnMediaPlayerChanged;
        _videoView.MediaPlayer = _mediaPlayer;

        _mediaPlayer.Playing  += (_, _) => PlaybackStarted?.Invoke(this, EventArgs.Empty);
        _mediaPlayer.EncounteredError += (_, _) => ErrorOccurred?.Invoke(this, "LibVLC playback error");
    }

    private void OnMediaPlayerChanged(object? sender, MediaPlayerChangedEventArgs e) { }

    public void SetSource(Uri uri)
    {
        _currentUri = uri;
        _currentMedia?.Dispose();
        _currentMedia = new Media(_libVLC, uri);
        // Forteaza TCP pentru RTSP — evita pierderea de pachete UDP
        if (uri.Scheme.StartsWith("rtsp", StringComparison.OrdinalIgnoreCase))
            _currentMedia.AddOption(":rtsp-tcp");
        _mediaPlayer.Media = _currentMedia;
    }

    public void Play()
    {
        if (_currentMedia == null) return;
        _mediaPlayer.Play();
    }

    public void Pause() => _mediaPlayer.Pause();

    public void Stop()
    {
        _mediaPlayer.Stop();
        _isRecording = false;
    }

    public Task<bool> TakeSnapshotAsync(string outputPath)
    {
        var result = _mediaPlayer.TakeSnapshot(0, outputPath, 0, 0);
        return Task.FromResult(result);
    }

    public bool StartRecording(string outputPath)
    {
        if (_currentUri == null || _isRecording) return false;

        _currentMedia?.Dispose();
        var escaped = outputPath.Replace("'", "\\'");
        var sout = $":sout=#duplicate{{dst=display,dst=std{{access=file,mux=mp4,dst='{escaped}'}}}}";

        _currentMedia = new Media(_libVLC, _currentUri);
        if (_currentUri.Scheme.StartsWith("rtsp", StringComparison.OrdinalIgnoreCase))
            _currentMedia.AddOption(":rtsp-tcp");
        _currentMedia.AddOption(sout);
        _currentMedia.AddOption(":sout-mp4-faststart");

        _mediaPlayer.Media = _currentMedia;
        _mediaPlayer.Play();
        _isRecording = true;
        return true;
    }

    public void StopRecording()
    {
        if (!_isRecording) return;
        _isRecording = false;
        // Reincarca media fara sout
        if (_currentUri != null) SetSource(_currentUri);
        _mediaPlayer.Play();
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _mediaPlayer.Stop();
        _currentMedia?.Dispose();
        _mediaPlayer.Dispose();
    }
}
