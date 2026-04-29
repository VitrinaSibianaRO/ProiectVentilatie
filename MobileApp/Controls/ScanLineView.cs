using SkiaSharp;
using SkiaSharp.Views.Maui;
using SkiaSharp.Views.Maui.Controls;

namespace ProiectVentilatie.Mobile.Controls;

/// <summary>
/// CRT-style animated scan line that sweeps top→bottom.
/// </summary>
public class ScanLineView : SKCanvasView
{
    private float _yPos = 0f;
    private IDispatcherTimer? _timer;

    protected override void OnHandlerChanged()
    {
        base.OnHandlerChanged();
        if (Handler != null)
        {
            _timer = Application.Current?.Dispatcher.CreateTimer();
            if (_timer == null) return;
            _timer.Interval = TimeSpan.FromMilliseconds(16);
            _timer.Tick += (_, _) =>
            {
                _yPos += 2.5f;
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

        float h = e.Info.Height;
        float w = e.Info.Width;

        if (_yPos > h + 20) _yPos = -20;

        using var paint = new SKPaint
        {
            IsAntialias = true,
            Shader = SKShader.CreateLinearGradient(
                new SKPoint(0, _yPos - 12),
                new SKPoint(0, _yPos + 12),
                new[] { SKColors.Transparent, SKColor.Parse("#00e6ff").WithAlpha(28), SKColors.Transparent },
                null,
                SKShaderTileMode.Clamp)
        };
        canvas.DrawRect(0, _yPos - 12, w, 24, paint);
    }
}
