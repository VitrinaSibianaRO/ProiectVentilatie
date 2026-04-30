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

        float width = info.Width;
        float height = info.Height;

        // Match React SVG viewport (36x36)
        float scaleX = width / 36f;
        float scaleY = height / 36f;
        canvas.Scale(scaleX, scaleY);

        float cx = 18f;
        float cy = 18f;

        SKColor cyan = SKColor.Parse("#00e6ff");

        // 1. Outer ring (circle cx="18" cy="18" r="15")
        using (var ringPaint = new SKPaint 
        { 
            Style = SKPaintStyle.Stroke, 
            Color = cyan.WithAlpha((byte)(IsOn ? 102 : 30)), // 0.4 : 0.12
            StrokeWidth = 1, 
            IsAntialias = true 
        })
        {
            using var ringFill = new SKPaint
            {
                Style = SKPaintStyle.Fill,
                Color = SKColor.Parse("#00b4ff").WithAlpha(18), // rgba(0,180,255,0.07)
                IsAntialias = true
            };
            canvas.DrawCircle(cx, cy, 15, ringFill);
            canvas.DrawCircle(cx, cy, 15, ringPaint);
        }

        // 2. Blades (rotated 5 times)
        canvas.Save();
        canvas.RotateDegrees(_rotation, cx, cy);

        using var bladePaint = new SKPaint 
        { 
            Style = SKPaintStyle.Fill, 
            IsAntialias = true, 
            Color = cyan.WithAlpha((byte)(IsOn ? 204 : 64)) // 0.8 : 0.25
        };

        for (int i = 0; i < 5; i++)
        {
            canvas.Save();
            canvas.RotateDegrees(i * 72f, cx, cy);
            // ellipse cx="18" cy="9" rx="4" ry="8"
            canvas.DrawOval(18, 9, 4, 8, bladePaint);
            canvas.Restore();
        }
        canvas.Restore();

        // 3. Hub (circle cx="18" cy="18" r="5")
        using (var hubFill = new SKPaint 
        { 
            Style = SKPaintStyle.Fill, 
            Color = (IsOn ? SKColor.Parse("#00d4ee") : SKColor.Parse("#1a4060")).WithAlpha((byte)(IsOn ? 230 : 153)), 
            IsAntialias = true 
        })
        {
            canvas.DrawCircle(cx, cy, 5, hubFill);
        }
    }
}
