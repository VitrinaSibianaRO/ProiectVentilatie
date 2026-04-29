using SkiaSharp;
using SkiaSharp.Views.Maui;
using SkiaSharp.Views.Maui.Controls;

namespace ProiectVentilatie.Mobile.Controls;

/// <summary>
/// Pulsating status badge — outer ring fades out while expanding.
/// Bind Color to your online/offline color.
/// </summary>
public class PulseBadgeView : SKCanvasView
{
    private float _pulse = 0f;
    private IDispatcherTimer? _timer;

    public static readonly BindableProperty BadgeColorProperty = BindableProperty.Create(
        nameof(BadgeColor), typeof(Color), typeof(PulseBadgeView), Colors.LimeGreen,
        propertyChanged: (b, o, n) => ((PulseBadgeView)b).InvalidateSurface());

    public Color BadgeColor
    {
        get => (Color)GetValue(BadgeColorProperty);
        set => SetValue(BadgeColorProperty, value);
    }

    protected override void OnHandlerChanged()
    {
        base.OnHandlerChanged();
        if (Handler != null)
        {
            _timer = Application.Current?.Dispatcher.CreateTimer();
            if (_timer == null) return;
            _timer.Interval = TimeSpan.FromMilliseconds(30);
            _timer.Tick += (_, _) =>
            {
                _pulse = (_pulse + 0.025f) % 1.0f;
                InvalidateSurface();
            };
            _timer.Start();
        }
        else
        {
            _timer?.Stop();
            _timer = null;
        }
    }

    protected override void OnPaintSurface(SKPaintSurfaceEventArgs e)
    {
        var canvas = e.Surface.Canvas;
        canvas.Clear(SKColors.Transparent);

        float cx = e.Info.Width / 2f;
        float cy = e.Info.Height / 2f;
        float coreR = Math.Min(cx, cy) * 0.38f;

        SKColor color = BadgeColor.ToSKColor();

        // Expanding pulse ring
        float ringR = coreR + (coreR * 2f * _pulse);
        byte alpha = (byte)(120 * (1f - _pulse));
        using var pulsePaint = new SKPaint
        {
            Style = SKPaintStyle.Fill,
            Color = color.WithAlpha(alpha),
            IsAntialias = true
        };
        canvas.DrawCircle(cx, cy, ringR, pulsePaint);

        // Core dot
        using var corePaint = new SKPaint
        {
            Style = SKPaintStyle.Fill,
            Color = color,
            IsAntialias = true
        };
        canvas.DrawCircle(cx, cy, coreR, corePaint);
    }
}
