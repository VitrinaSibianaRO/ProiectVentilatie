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
        
        // Match React SVG viewport (40x56)
        float scaleX = width / 40f;
        float scaleY = height / 56f;
        canvas.Scale(scaleX, scaleY);

        float pct = (float)Math.Clamp(Humidity / 100.0, 0, 1);
        SKColor color = GetHumidityColor(Humidity);
        string glow = GetGlowColor(Humidity);

        // Path from SVG: M20 2C20 2 4 22 4 36C4 46.5 11.2 54 20 54C28.8 54 36 46.5 36 36C36 22 20 2 20 2Z
        using var path = SKPath.ParseSvgPathData("M20 2C20 2 4 22 4 36C4 46.5 11.2 54 20 54C28.8 54 36 46.5 36 36C36 22 20 2 20 2Z");

        // 1. Background (fill="rgba(255,255,255,0.06)" stroke="rgba(255,255,255,0.18)")
        using var bgFillPaint = new SKPaint
        {
            Color = new SKColor(255, 255, 255, 15), // 0.06
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        using var bgStrokePaint = new SKPaint
        {
            Color = new SKColor(255, 255, 255, 46), // 0.18
            Style = SKPaintStyle.Stroke,
            StrokeWidth = 1.2f,
            IsAntialias = true
        };
        canvas.DrawPath(path, bgFillPaint);
        canvas.DrawPath(path, bgStrokePaint);

        // 2. Fill with Gradient (dg + uid)
        canvas.Save();
        canvas.ClipPath(path);

        float fillY = 56 * (1 - pct);
        using var fillPaint = new SKPaint
        {
            Shader = SKShader.CreateLinearGradient(
                new SKPoint(20, 0),
                new SKPoint(20, 56),
                new SKColor[] { color.WithAlpha(230), color.WithAlpha(140) }, // 0.9 to 0.55
                null,
                SKShaderTileMode.Clamp),
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        canvas.DrawRect(0, fillY, 40, 56, fillPaint);
        canvas.Restore();

        // 3. Highlight (ellipse cx="14" cy="24" rx="4" ry="6" transform="rotate(-20,14,24)")
        canvas.Save();
        canvas.RotateDegrees(-20, 14, 24);
        using var highlightPaint = new SKPaint
        {
            Color = SKColors.White.WithAlpha(25), // 0.1
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        canvas.DrawOval(14, 24, 4, 6, highlightPaint);
        canvas.Restore();

        // 4. Glow Border
        using var glowPaint = new SKPaint
        {
            Color = SKColor.Parse(glow),
            Style = SKPaintStyle.Stroke,
            StrokeWidth = 1f,
            IsAntialias = true,
            ImageFilter = SKImageFilter.CreateBlur(3, 3)
        };
        canvas.DrawPath(path, glowPaint);
    }

    private SKColor GetHumidityColor(double h)
    {
        if (h < 30) return SKColor.Parse("#ff9944");
        if (h < 45) return SKColor.Parse("#ffdd44");
        if (h <= 52) return SKColor.Parse("#00e87a");
        if (h < 65) return SKColor.Parse("#44aaff");
        return SKColor.Parse("#8855ff");
    }

    private string GetGlowColor(double h)
    {
        if (h < 30)  return "rgba(255,153,68,0.4)";
        if (h < 45)  return "rgba(255,221,68,0.4)";
        if (h <= 52) return "rgba(0,232,122,0.4)";
        if (h < 65)  return "rgba(68,170,255,0.4)";
        return "rgba(136,85,255,0.4)";
    }
}
