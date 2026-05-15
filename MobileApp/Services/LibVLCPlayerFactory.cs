using LibVLCSharp.Shared;

namespace ProiectVentilatie.Mobile.Services;

public class LibVLCPlayerFactory : IPlayerFactory, IDisposable
{
    private readonly LibVLC _libVLC;
    public bool IsLibVlc => true;

    public LibVLCPlayerFactory()
    {
        // network-caching: 1000ms — balanseaza latenta vs buffering
        _libVLC = new LibVLC("--rtsp-tcp", "--network-caching=1000", "--live-caching=1000");
    }

    public IVideoPlayerHandle Create() => new LibVLCPlayerHandle(_libVLC);

    public void Dispose() => _libVLC.Dispose();
}
