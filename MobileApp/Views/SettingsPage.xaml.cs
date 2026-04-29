using ProiectVentilatie.Mobile.ViewModels;

namespace ProiectVentilatie.Mobile.Views;

public partial class SettingsPage : ContentPage
{
    public SettingsPage(SettingsViewModel viewModel)
    {
        InitializeComponent();
        BindingContext = viewModel;
    }
}
