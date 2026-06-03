using ProiectVentilatie.Mobile.ViewModels;

namespace ProiectVentilatie.Mobile.Views;

public partial class SettingsPage : ContentPage
{
    private readonly SettingsViewModel _vm;

    public SettingsPage(SettingsViewModel viewModel)
    {
        InitializeComponent();
        BindingContext = _vm = viewModel;
    }

}
