using System.Text.Json;
using HiveMQtt.Client;
using HiveMQtt.Client.Options;
using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public class MqttService : IMqttService
{
    private HiveMQClient _client;
    public event Action<VentilationState>? OnStateReceived;
    public event Action<bool>? OnConnectionChanged;

    public MqttService()
    {
        var options = new HiveMQClientOptions
        {
            Host = "your-cluster-url.s1.eu.hivemq.cloud",
            Port = 8883,
            UseTLS = true,
            UserName = "YOUR_USERNAME",
            Password = "YOUR_PASSWORD"
        };

        _client = new HiveMQClient(options);
        
        _client.OnMessageReceived += (sender, args) =>
        {
            if (args.PublishMessage.Topic == "ventilation/state")
            {
                var payload = args.PublishMessage.PayloadAsString;
                try
                {
                    var state = JsonSerializer.Deserialize<VentilationState>(payload);
                    if (state != null)
                    {
                        MainThread.BeginInvokeOnMainThread(() => OnStateReceived?.Invoke(state));
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Parse error: {ex.Message}");
                }
            }
        };
    }

    public async Task ConnectAsync()
    {
        try
        {
            var connectResult = await _client.ConnectAsync();
            await _client.SubscribeAsync("ventilation/state");
            MainThread.BeginInvokeOnMainThread(() => OnConnectionChanged?.Invoke(true));
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Connect error: {ex.Message}");
            MainThread.BeginInvokeOnMainThread(() => OnConnectionChanged?.Invoke(false));
        }
    }

    public async Task SendCommandAsync(object command)
    {
        if (_client.IsConnected())
        {
            var json = JsonSerializer.Serialize(command);
            await _client.PublishAsync("ventilation/command", json);
        }
    }
}
