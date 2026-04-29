using CommunityToolkit.Maui.Views;
using ProiectVentilatie.Mobile.Controls;

namespace ProiectVentilatie.Mobile.Services;

public static class ToastService
{
    public static void Show(string message, string icon = "ℹ️")
    {
        var page = Application.Current?.Windows[0]?.Page;
        if (page == null) return;

        var popup = new IndustrialToast(message, icon);
        page.ShowPopup(popup);

        // Auto-dismiss after 2.5 seconds using the dispatcher (no deprecated Device.StartTimer)
        Application.Current?.Dispatcher.DispatchDelayed(
            TimeSpan.FromMilliseconds(2500),
            () => popup.Close());
    }
}
