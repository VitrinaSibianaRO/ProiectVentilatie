namespace ProiectVentilatie.Mobile.Models;

/// <summary>
/// Configurare MQTT deserializată din appsettings.json secțiunea "Mqtt".
/// Injectat via IOptions&lt;MqttSettings&gt; în MqttService.
/// </summary>
public class MqttSettings
{
    public string Host { get; set; } = string.Empty;
    public int Port { get; set; } = 8883;
    public string Username { get; set; } = string.Empty;
    public string Password { get; set; } = string.Empty;

    public string StateTopic { get; set; } = "ventilatie/state";
    public string CommandTopic { get; set; } = "ventilatie/cmd";
    public string OnlineTopic { get; set; } = "ventilatie/online";
    public string EventTopic { get; set; } = "ventilatie/event";
    public string LogTopic { get; set; } = "ventilatie/log";
    public string TvStateTopic { get; set; } = "ventilatie/tv/state";

    public int ReconnectInitialMs { get; set; } = 1000;
    public int ReconnectMaxMs { get; set; } = 30000;
}
