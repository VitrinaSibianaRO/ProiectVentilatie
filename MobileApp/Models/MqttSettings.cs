namespace ProiectVentilatie.Mobile.Models;

/// <summary>
/// Configurare MQTT deserializată din appsettings.json secțiunea "Mqtt".
/// Injectat via IOptions<MqttSettings> în MqttService.
/// </summary>
public class MqttSettings
{
    public string Host { get; set; } = string.Empty;
    public int Port { get; set; } = 8883;
    public string UserName { get; set; } = string.Empty;
    public string Password { get; set; } = string.Empty;

    public string TopicState { get; set; } = "ventilatie/state";
    public string TopicCmd { get; set; } = "ventilatie/cmd";
    public string TopicOnline { get; set; } = "ventilatie/online";
    public string TopicEvent { get; set; } = "ventilatie/event";
    public string TopicLog { get; set; } = "ventilatie/log";
}
