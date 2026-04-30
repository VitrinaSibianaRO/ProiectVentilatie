using SkiaSharp;
using SkiaSharp.Views.Maui;
using SkiaSharp.Views.Maui.Controls;

namespace ProiectVentilatie.Mobile.Controls;

public class ThermometerView : SKCanvasView
{
    public static readonly BindableProperty TemperatureProperty = BindableProperty.Create(
        nameof(Temperature), typeof(double), typeof(ThermometerView), 0.0, propertyChanged: (b, o, n) => ((ThermometerView)b).InvalidateSurface());

    public double Temperature { get => (double)GetValue(TemperatureProperty); set => SetValue(TemperatureProperty, value); }

    protected override void OnPaintSurface(SKPaintSurfaceEventArgs e)
    {
        var canvas = e.Surface.Canvas;
        var info = e.Info;

        canvas.Clear();

        float width = info.Width;
        float height = info.Height;
        
        // Match React SVG viewport (28x80)
        float scaleX = width / 28f;
        float scaleY = height / 80f;
        canvas.Scale(scaleX, scaleY);

        float pct = (float)Math.Clamp(Temperature / 60.0, 0, 1);
        SKColor color = GetTemperatureColor(Temperature);
        string glow = GetGlowColor(Temperature);

        // 1. Tube Background (rect x="10" y="6" width="8" height="52")
        using var tubeBgPaint = new SKPaint
        {
            Color = new SKColor(255, 255, 255, 18), // rgba(255,255,255,0.07)
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        using var tubeStrokePaint = new SKPaint
        {
            Color = new SKColor(255, 255, 255, 46), // rgba(255,255,255,0.18)
            Style = SKPaintStyle.Stroke,
            StrokeWidth = 1,
            IsAntialias = true
        };
        canvas.DrawRoundRect(10, 6, 8, 52, 4, 4, tubeBgPaint);
        canvas.DrawRoundRect(10, 6, 8, 52, 4, 4, tubeStrokePaint);

        // 2. Mercury Gradient (from tg00aaff)
        float levelY = 6 + 52 * (1 - pct);
        float levelHeight = 52 * pct;

        using var mercuryPaint = new SKPaint
        {
            Shader = SKShader.CreateLinearGradient(
                new SKPoint(14, 58),
                new SKPoint(14, 6),
                new SKColor[] { 
                    SKColor.Parse("#00aaff"), 
                    SKColor.Parse("#00ddcc"), 
                    SKColor.Parse("#00e87a"), 
                    SKColor.Parse("#ffbb00"), 
                    SKColor.Parse("#ff4422") 
                },
                new float[] { 0.0f, 0.35f, 0.60f, 0.80f, 1.0f },
                SKShaderTileMode.Clamp),
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };

        // Clip to mercury level
        canvas.Save();
        canvas.ClipRect(new SKRect(11, levelY, 17, 58));
        canvas.DrawRoundRect(11, 6, 6, 52, 3, 3, mercuryPaint);
        canvas.Restore();

        // 3. Bulb (circle cx="14" cy="64" r="9")
        using var bulbPaint = new SKPaint
        {
            Color = color,
            Style = SKPaintStyle.Fill,
            IsAntialias = true,
            ImageFilter = SKImageFilter.CreateBlur(2, 2)
        };
        // Glow effect
        using var bulbGlowPaint = new SKPaint
        {
            Color = SKColor.Parse(glow),
            Style = SKPaintStyle.Fill,
            IsAntialias = true,
            ImageFilter = SKImageFilter.CreateBlur(5, 5)
        };
        canvas.DrawCircle(14, 64, 9, bulbGlowPaint);
        
        bulbPaint.ImageFilter = null;
        canvas.DrawCircle(14, 64, 9, bulbPaint);

        // 4. Highlight (circle cx="11" cy="61" r="3")
        using var highlightPaint = new SKPaint
        {
            Color = SKColors.White.WithAlpha(50),
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        canvas.DrawCircle(11, 61, 3, highlightPaint);

        // 5. Scale Lines
        using var scalePaint = new SKPaint
        {
            Color = SKColors.White.WithAlpha(64), // rgba(255,255,255,0.25)
            StrokeWidth = 0.8f,
            IsAntialias = true
        };
        foreach (int p in new int[] { 0, 25, 50, 75, 100 })
        {
            float y = 6 + 52 * (1 - p / 100f);
            float x2 = p == 50 ? 24 : 22;
            canvas.DrawLine(19, y, x2, y, scalePaint);
        }
    }

    private SKColor GetTemperatureColor(double temp)
    {
        if (temp <= 18) return SKColor.Parse("#00aaff");
        if (temp < 25) return SKColor.Parse("#00ddcc");
        if (temp <= 33) return SKColor.Parse("#00e87a");
        if (temp < 38) return SKColor.Parse("#ffbb00");
        return SKColor.Parse("#ff4422");
    }

    private string GetGlowColor(double temp)
    {
        if (temp <= 18) return "rgba(0,170,255,0.4)";
        if (temp < 25)  return "rgba(0,220,200,0.4)";
        if (temp <= 33) return "rgba(0,232,122,0.4)";
        if (temp < 38)  return "rgba(255,187,0,0.4)";
        return "rgba(255,68,34,0.4)";
    }
}
