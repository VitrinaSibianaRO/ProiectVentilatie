using ProiectVentilatie.Mobile.ViewModels;

namespace ProiectVentilatie.Mobile.Views;

public partial class LogPage : ContentPage
{
    private readonly LogViewModel _viewModel;

    public LogPage(LogViewModel viewModel)
    {
        InitializeComponent();
        _viewModel = viewModel;
        BindingContext = _viewModel;
    }

    protected override void OnAppearing()
    {
        base.OnAppearing();
        // Auto-load la prima deschidere a tab-ului
        if (_viewModel.Entries.Count == 0)
        {
            _viewModel.ReloadCommand.Execute(null);
        }
    }
}
