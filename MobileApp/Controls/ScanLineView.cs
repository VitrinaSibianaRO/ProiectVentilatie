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
        try
        {
            base.OnHandlerChanged();
        }
        catch
        {
            IsVisible = false;
            return;
        }

        if (Handler != null)
        {
            try
            {
                _timer = Application.Current?.Dispatcher.CreateTimer();
                if (_timer == null) return;
                _timer.Interval = TimeSpan.FromMilliseconds(33); // ~30fps — mai puțin agresiv
                _timer.Tick += (_, _) =>
                {
                    _yPos += 2.5f;
                    InvalidateSurface();
                };
                _timer.Start();
            }
            catch
            {
                IsVisible = false;
            }
        }
        else
        {
            _timer?.Stop();
            _timer = null;
        }
    }

    protected override void OnPaintSurface(SKPaintSurfaceEventArgs e)
    {
        try
        {
            var canvas = e.Surface.Canvas;
            canvas.Clear(SKColors.Transparent);

            float h = e.Info.Height;
            float w = e.Info.Width;

            if (_yPos > h + 20) _yPos = -20;

            using var paint = new SKPaint
            {
                IsAntialias = false,
                Shader = SKShader.CreateLinearGradient(
                    new SKPoint(0, _yPos - 12),
                    new SKPoint(0, _yPos + 12),
                    new[] { SKColors.Transparent, SKColor.Parse("#00e6ff").WithAlpha(28), SKColors.Transparent },
                    null,
                    SKShaderTileMode.Clamp)
            };
            canvas.DrawRect(0, _yPos - 12, w, 24, paint);
        }
        catch
        {
            // GPU unavailable (Waydroid/emulator) — skip frame silently
        }
    }
}
