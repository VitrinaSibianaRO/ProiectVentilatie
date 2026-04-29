using ProiectVentilatie.Mobile.ViewModels;

namespace ProiectVentilatie.Mobile.Views;

public partial class SystemPage : ContentPage
{
    public SystemPage(SystemViewModel viewModel)
    {
        InitializeComponent();
        BindingContext = viewModel;
    }
}
