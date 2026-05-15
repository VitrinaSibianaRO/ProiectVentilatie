namespace ProiectVentilatie.Mobile.Services;

public interface IPlayerFactory
{
    IVideoPlayerHandle Create();
    bool IsLibVlc { get; }
}
