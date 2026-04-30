using System.Collections.ObjectModel;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

/// <summary>
/// Tab Log evenimente — listă cu evenimente persistate de ESP32 în NVS.
/// Trimite cmd:getLog → așteaptă răspuns pe ventilatie/log → afișează.
/// </summary>
public partial class LogViewModel : ObservableObject, IDisposable
{
    private readonly IMqttService _mqttService;

    public ObservableCollection<LogEntry> Entries { get; } = new();
    public ObservableCollection<string> AvailableFilters { get; } = new()
    {
        "Toate", "Releu", "Senzor", "Override"
    };

    [ObservableProperty] private bool _isLoading;
    [ObservableProperty] private string _statusMessage = string.Empty;
    [ObservableProperty] private string _selectedFilter = "Toate";
    [ObservableProperty] private int _entriesCount;

    private List<LogEntry> _allEntries = new();

    public LogViewModel(IMqttService mqttService)
    {
        _mqttService = mqttService;
        _mqttService.OnLogReceived += OnLogReceived;
    }

    private void OnLogReceived(string json)
    {
        IsLoading = false;

        try
        {
            var response = JsonSerializer.Deserialize<LogResponse>(json);
            if (response == null)
            {
                StatusMessage = "Răspuns gol de la ESP32.";
                return;
            }

            _allEntries = response.Entries ?? new List<LogEntry>();
            ApplyFilter();
            StatusMessage = $"✓ {_allEntries.Count} evenimente încărcate";
        }
        catch (Exception ex)
        {
            StatusMessage = $"✗ Eroare parse: {ex.Message}";
        }
    }

    partial void OnSelectedFilterChanged(string value) => ApplyFilter();

    [RelayCommand]
    private void SetFilter(string filter) => SelectedFilter = filter;

    private void ApplyFilter()
    {
        var typeKey = SelectedFilter switch
        {
            "Releu"    => "relay_change",
            "Senzor"   => "sensor_err",
            "Override" => "override_expired",
            _          => (string?)null
        };

        Entries.Clear();
        var filtered = typeKey == null
            ? _allEntries
            : _allEntries.Where(e => e.Type == typeKey);

        foreach (var entry in filtered.Reverse())
            Entries.Add(entry);
        EntriesCount = Entries.Count;
    }

    [RelayCommand]
    private async Task ReloadAsync()
    {
        if (!_mqttService.IsConnected)
        {
            StatusMessage = "✗ Nu sunt conectat la broker";
            return;
        }
        IsLoading = true;
        StatusMessage = "Cerere log... aștept ESP32";
        await _mqttService.SendCommandAsync(new { cmd = "getLog" });
    }

    public void Dispose()
    {
        _mqttService.OnLogReceived -= OnLogReceived;
    }
}
