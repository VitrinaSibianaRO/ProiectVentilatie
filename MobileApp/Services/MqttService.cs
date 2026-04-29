using System.Text.Json;
using HiveMQtt.Client;
using HiveMQtt.Client.Options;
using HiveMQtt.MQTT5.Types;
using Microsoft.Extensions.Options;
using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

/// <summary>
/// Serviciu MQTT conectat la HiveMQ Cloud.
/// - Config via DI (IOptions&lt;MqttSettings&gt;)
/// - Reconnect cu exponential backoff (5s→60s)
/// - Lifecycle: ConnectAsync/DisconnectAsync (OnSleep/OnResume)
/// - Subscribe: state (retained), online, event, log
/// - LastState cached — disponibil instant la connect
/// </summary>
public class MqttService : IMqttService, IDisposable
{
    private readonly MqttSettings _settings;
    private HiveMQClient? _client;
    private bool _disposed;
    private bool _intentionalDisconnect;
    private CancellationTokenSource? _reconnectCts;

    // Backoff reconnect
    private int _reconnectDelayMs = 5000;
    private const int MaxReconnectDelayMs = 60000;

    public event Action<VentilationState>? OnStateReceived;
    public event Action<bool>? OnConnectionChanged;
    public event Action<string>? OnOnlineStatusChanged;
    public event Action<string>? OnEventReceived;
    public event Action<string>? OnLogReceived;

    public VentilationState? LastState { get; private set; }
    public bool IsConnected => _client?.IsConnected() ?? false;

    public MqttService(IOptions<MqttSettings> options)
    {
        _settings = options.Value;
    }

    public async Task ConnectAsync()
    {
        _intentionalDisconnect = false;
        _reconnectDelayMs = 5000;

        if (_client != null && _client.IsConnected())
            return;

        _reconnectCts?.Cancel();
        _reconnectCts = new CancellationTokenSource();

        await ConnectInternalAsync();
    }

    public async Task DisconnectAsync()
    {
        _intentionalDisconnect = true;
        _reconnectCts?.Cancel();

        if (_client != null && _client.IsConnected())
        {
            try
            {
                await _client.DisconnectAsync();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[MQTT] Disconnect error: {ex.Message}");
            }
        }

        MainThread.BeginInvokeOnMainThread(() => OnConnectionChanged?.Invoke(false));
    }

    public async Task SendCommandAsync(object command)
    {
        if (_client == null || !_client.IsConnected())
            return;

        try
        {
            var json = JsonSerializer.Serialize(command);
            await _client.PublishAsync(_settings.TopicCmd, json, QualityOfService.AtLeastOnceDelivery);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[MQTT] Send error: {ex.Message}");
        }
    }

    private async Task ConnectInternalAsync()
    {
        try
        {
            var options = new HiveMQClientOptions
            {
                Host = _settings.Host,
                Port = _settings.Port,
                UseTLS = true,
                UserName = _settings.UserName,
                Password = _settings.Password,
                ClientId = $"maui-{Guid.NewGuid():N}".Substring(0, 23),
                CleanStart = true,
                KeepAlive = 60,
            };

            _client = new HiveMQClient(options);

            // Message handler — routing pe topic
            _client.OnMessageReceived += OnMessageReceived;

            // Disconnect handler — reconnect automat
            _client.AfterDisconnect += async (sender, args) =>
            {
                MainThread.BeginInvokeOnMainThread(() => OnConnectionChanged?.Invoke(false));

                if (!_intentionalDisconnect)
                {
                    Console.WriteLine($"[MQTT] Disconnected. Reconnect in {_reconnectDelayMs}ms...");
                    await ReconnectWithBackoffAsync();
                }
            };

            await _client.ConnectAsync();

            // Subscribe la toate topic-urile relevante
            await _client.SubscribeAsync(_settings.TopicState, QualityOfService.AtMostOnceDelivery);
            await _client.SubscribeAsync(_settings.TopicOnline, QualityOfService.AtLeastOnceDelivery);
            await _client.SubscribeAsync(_settings.TopicEvent, QualityOfService.AtMostOnceDelivery);
            await _client.SubscribeAsync(_settings.TopicLog, QualityOfService.AtLeastOnceDelivery);

            _reconnectDelayMs = 5000; // Reset backoff
            MainThread.BeginInvokeOnMainThread(() => OnConnectionChanged?.Invoke(true));
            Console.WriteLine("[MQTT] Connected + subscribed.");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[MQTT] Connect error: {ex.Message}");
            MainThread.BeginInvokeOnMainThread(() => OnConnectionChanged?.Invoke(false));

            if (!_intentionalDisconnect)
            {
                await ReconnectWithBackoffAsync();
            }
        }
    }

    private async Task ReconnectWithBackoffAsync()
    {
        try
        {
            var token = _reconnectCts?.Token ?? CancellationToken.None;
            await Task.Delay(_reconnectDelayMs, token);

            // Exponential backoff
            _reconnectDelayMs = Math.Min(_reconnectDelayMs * 2, MaxReconnectDelayMs);

            if (!token.IsCancellationRequested)
            {
                await ConnectInternalAsync();
            }
        }
        catch (TaskCanceledException)
        {
            // Reconnect anulat — lifecycle DisconnectAsync
        }
    }

    private void OnMessageReceived(object? sender, HiveMQtt.Client.Events.OnMessageReceivedEventArgs args)
    {
        var topic = args.PublishMessage.Topic;
        var payload = args.PublishMessage.PayloadAsString;

        if (string.IsNullOrEmpty(payload)) return;

        try
        {
            if (topic == _settings.TopicState)
            {
                var state = JsonSerializer.Deserialize<VentilationState>(payload);
                if (state != null)
                {
                    LastState = state;
                    MainThread.BeginInvokeOnMainThread(() => OnStateReceived?.Invoke(state));
                }
            }
            else if (topic == _settings.TopicOnline)
            {
                MainThread.BeginInvokeOnMainThread(() => OnOnlineStatusChanged?.Invoke(payload));
            }
            else if (topic == _settings.TopicEvent)
            {
                MainThread.BeginInvokeOnMainThread(() => OnEventReceived?.Invoke(payload));
            }
            else if (topic == _settings.TopicLog)
            {
                MainThread.BeginInvokeOnMainThread(() => OnLogReceived?.Invoke(payload));
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[MQTT] Parse error on {topic}: {ex.Message}");
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _reconnectCts?.Cancel();
        _reconnectCts?.Dispose();

        if (_client != null)
        {
            _client.OnMessageReceived -= OnMessageReceived;
            try { _client.DisconnectAsync().Wait(2000); } catch { }
            _client.Dispose();
        }
    }
}
