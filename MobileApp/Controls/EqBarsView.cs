using SkiaSharp;
using SkiaSharp.Views.Maui;
using SkiaSharp.Views.Maui.Controls;

namespace ProiectVentilatie.Mobile.Controls;

public class EqBarsView : SKCanvasView
{
    private readonly float[] _amplitudes = new float[22];
    private readonly float[] _speeds = new float[22];
    private float _phase = 0;
    private IDispatcherTimer? _timer;

    public static readonly BindableProperty IsActiveProperty = BindableProperty.Create(
        nameof(IsActive), typeof(bool), typeof(EqBarsView), false, propertyChanged: OnIsActiveChanged);

    public bool IsActive { get => (bool)GetValue(IsActiveProperty); set => SetValue(IsActiveProperty, value); }

    public EqBarsView()
    {
        var rnd = new Random();
        for (int i = 0; i < 22; i++)
        {
            _amplitudes[i] = 0.3f + (float)rnd.NextDouble() * 0.7f;
            _speeds[i] = 0.05f + (float)rnd.NextDouble() * 0.1f;
        }
    }

    private static void OnIsActiveChanged(BindableObject bindable, object oldValue, object newValue)
    {
        var view = (EqBarsView)bindable;
        if ((bool)newValue) view.StartAnimation();
        else view.StopAnimation();
    }

    private void StartAnimation()
    {
        if (_timer != null) return;
        _timer = Application.Current?.Dispatcher.CreateTimer();
        if (_timer == null) return;
        _timer.Interval = TimeSpan.FromMilliseconds(32);
        _timer.Tick += (s, e) =>
        {
            _phase += 0.15f;
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
        int barCount = 22;
        float gap = 4f;
        float barWidth = (width - (gap * (barCount - 1))) / barCount;

        using var paint = new SKPaint
        {
            Style = SKPaintStyle.Fill,
            Color = SKColor.Parse("#00e6ff"),
            IsAntialias = true
        };

        for (int i = 0; i < barCount; i++)
        {
            float val = IsActive 
                ? (float)Math.Abs(Math.Sin(_phase * _speeds[i] * 10 + i * 0.7f)) * _amplitudes[i]
                : 0.05f;

            float barHeight = Math.Max(4, height * val);
            float x = i * (barWidth + gap);
            float y = height - barHeight;

            paint.Color = SKColor.Parse("#00e6ff").WithAlpha((byte)(IsActive ? 40 + val * 180 : 30));
            
            canvas.DrawRoundRect(x, y, barWidth, barHeight, 2, 2, paint);
        }
    }
}
