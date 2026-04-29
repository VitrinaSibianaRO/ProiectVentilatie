using SkiaSharp;
using SkiaSharp.Views.Maui;
using SkiaSharp.Views.Maui.Controls;

namespace ProiectVentilatie.Mobile.Controls;

public class DropletView : SKCanvasView
{
    public static readonly BindableProperty HumidityProperty = BindableProperty.Create(
        nameof(Humidity), typeof(double), typeof(DropletView), 0.0, propertyChanged: (b, o, n) => ((DropletView)b).InvalidateSurface());

    public double Humidity { get => (double)GetValue(HumidityProperty); set => SetValue(HumidityProperty, value); }

    protected override void OnPaintSurface(SKPaintSurfaceEventArgs e)
    {
        var canvas = e.Surface.Canvas;
        var info = e.Info;

        canvas.Clear();

        float width = info.Width;
        float height = info.Height;
        float pct = (float)Math.Clamp(Humidity / 100.0, 0, 1);
        SKColor color = GetHumidityColor(Humidity);

        using var path = new SKPath();
        path.MoveTo(width / 2, 5);
        path.QuadTo(width * 0.9f, height * 0.5f, width * 0.9f, height * 0.75f);
        path.ArcTo(new SKRect(width * 0.1f, height * 0.5f, width * 0.9f, height * 0.95f), 0, 180, false);
        path.QuadTo(width * 0.1f, height * 0.5f, width / 2, 5);
        path.Close();

        // Background
        using var bgPaint = new SKPaint
        {
            Color = new SKColor(255, 255, 255, 15),
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        canvas.DrawPath(path, bgPaint);

        // Clip and Fill
        canvas.Save();
        canvas.ClipPath(path);
        
        var fillRect = new SKRect(0, height * (1 - pct), width, height);
        using var fillPaint = new SKPaint
        {
            Color = color,
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        canvas.DrawRect(fillRect, fillPaint);
        canvas.Restore();

        // Border
        using var borderPaint = new SKPaint
        {
            Color = color.WithAlpha(100),
            Style = SKPaintStyle.Stroke,
            StrokeWidth = 2,
            IsAntialias = true
        };
        canvas.DrawPath(path, borderPaint);
    }

    private SKColor GetHumidityColor(double h)
    {
        if (h < 30) return SKColor.Parse("#ff9944");
        if (h < 45) return SKColor.Parse("#ffdd44");
        if (h <= 52) return SKColor.Parse("#00e87a");
        if (h < 65) return SKColor.Parse("#44aaff");
        return SKColor.Parse("#8855ff");
    }
}
