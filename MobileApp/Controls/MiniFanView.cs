using SkiaSharp;
using SkiaSharp.Views.Maui;
using SkiaSharp.Views.Maui.Controls;

namespace ProiectVentilatie.Mobile.Controls;

public class MiniFanView : SKCanvasView
{
    private float _rotation = 0;
    private IDispatcherTimer? _timer;

    public static readonly BindableProperty IsOnProperty = BindableProperty.Create(
        nameof(IsOn), typeof(bool), typeof(MiniFanView), false, propertyChanged: OnIsOnChanged);

    public bool IsOn { get => (bool)GetValue(IsOnProperty); set => SetValue(IsOnProperty, value); }

    private static void OnIsOnChanged(BindableObject bindable, object oldValue, object newValue)
    {
        var view = (MiniFanView)bindable;
        if ((bool)newValue)
            view.StartAnimation();
        else
            view.StopAnimation();
    }

    private void StartAnimation()
    {
        if (_timer != null) return;
        _timer = Application.Current?.Dispatcher.CreateTimer();
        if (_timer == null) return;
        _timer.Interval = TimeSpan.FromMilliseconds(16);
        _timer.Tick += (s, e) =>
        {
            _rotation = (_rotation + 8f) % 360f;
            InvalidateSurface();
        };
        _timer.Start();
    }

    private void StopAnimation()
    {
        _timer?.Stop();
        _timer = null;
        InvalidateSurface();
    }

    protected override void OnPaintSurface(SKPaintSurfaceEventArgs e)
    {
        var canvas = e.Surface.Canvas;
        var info = e.Info;
        canvas.Clear();

        float cx = info.Width / 2f;
        float cy = info.Height / 2f;
        float radius = Math.Min(cx, cy) * 0.88f;

        SKColor accent = IsOn ? SKColor.Parse("#00e6ff") : new SKColor(80, 100, 120);

        // Outer ring
        using (var ringPaint = new SKPaint { Style = SKPaintStyle.Stroke, Color = accent.WithAlpha(80), StrokeWidth = 2, IsAntialias = true })
            canvas.DrawCircle(cx, cy, radius, ringPaint);

        // Blades
        canvas.Save();
        canvas.RotateDegrees(_rotation, cx, cy);

        using var bladePaint = new SKPaint { Style = SKPaintStyle.Fill, IsAntialias = true, Color = accent };

        for (int i = 0; i < 5; i++)
        {
            canvas.Save();
            canvas.RotateDegrees(i * 72f, cx, cy);
            var bladeRect = new SKRect(cx - radius * 0.18f, cy - radius * 0.88f, cx + radius * 0.18f, cy - radius * 0.12f);
            canvas.DrawOval(bladeRect, bladePaint);
            canvas.Restore();
        }
        canvas.Restore();

        // Hub
        using var hubFill = new SKPaint { Style = SKPaintStyle.Fill, Color = accent, IsAntialias = true };
        canvas.DrawCircle(cx, cy, radius * 0.22f, hubFill);
        using var hubRing = new SKPaint { Style = SKPaintStyle.Stroke, Color = new SKColor(0, 8, 20), StrokeWidth = 2, IsAntialias = true };
        canvas.DrawCircle(cx, cy, radius * 0.22f, hubRing);
    }
}
