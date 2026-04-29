using CommunityToolkit.Maui.Views;

namespace ProiectVentilatie.Mobile.Controls;

public partial class IndustrialToast : Popup
{
    public IndustrialToast(string message, string icon = "ℹ️")
    {
        InitializeComponent();
        MessageLabel.Text = message;
        IconLabel.Text = icon;
    }
}
