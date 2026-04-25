using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

public partial class DashboardViewModel : ObservableObject
{
    private readonly IMqttService _mqttService;

    [ObservableProperty]
    private float _leftTemp;

    [ObservableProperty]
    private float _leftHum;

    [ObservableProperty]
    private bool _leftRelay;

    [ObservableProperty]
    private float _rightTemp;

    [ObservableProperty]
    private float _rightHum;

    [ObservableProperty]
    private bool _rightRelay;

    [ObservableProperty]
    private string _connectionStatus = "Deconectat";

    public DashboardViewModel(IMqttService mqttService)
    {
        _mqttService = mqttService;
        _mqttService.OnStateReceived += UpdateState;
        _mqttService.OnConnectionChanged += UpdateConnection;
        
        _ = ConnectAsync();
    }

    private async Task ConnectAsync()
    {
        ConnectionStatus = "Conectare...";
        await _mqttService.ConnectAsync();
    }

    private void UpdateConnection(bool isConnected)
    {
        ConnectionStatus = isConnected ? "Conectat la HiveMQ" : "Deconectat";
        if (isConnected) RefreshCommand.Execute(null);
    }

    private void UpdateState(VentilationState state)
    {
        LeftTemp = state.Left.Temp;
        LeftHum = state.Left.Hum;
        LeftRelay = state.Left.Relay;

        RightTemp = state.Right.Temp;
        RightHum = state.Right.Hum;
        RightRelay = state.Right.Relay;
    }

    [RelayCommand]
    private async Task RefreshAsync()
    {
        await _mqttService.SendCommandAsync(new { cmd = "refresh" });
    }

    [RelayCommand]
    private async Task ToggleLeftAsync()
    {
        await _mqttService.SendCommandAsync(new { cmd = "setRelay", zone = "left", state = !LeftRelay });
    }

    [RelayCommand]
    private async Task ToggleRightAsync()
    {
        await _mqttService.SendCommandAsync(new { cmd = "setRelay", zone = "right", state = !RightRelay });
    }
}
