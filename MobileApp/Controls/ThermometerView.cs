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
        float padding = 10;
        
        float tubeWidth = width * 0.3f;
        float bulbRadius = width * 0.35f;
        float tubeHeight = height - bulbRadius * 2 - padding * 2;
        
        float centerX = width / 2;
        float bulbY = height - bulbRadius - padding;
        float tubeTopY = padding;

        // Draw Tube Background
        using var tubeBgPaint = new SKPaint
        {
            Color = new SKColor(255, 255, 255, 15),
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        canvas.DrawRoundRect(centerX - tubeWidth / 2, tubeTopY, tubeWidth, tubeHeight + bulbRadius, tubeWidth / 2, tubeWidth / 2, tubeBgPaint);

        // Draw Bulb
        float pct = (float)Math.Clamp(Temperature / 60.0, 0, 1);
        SKColor color = GetTemperatureColor(Temperature);

        using var bulbPaint = new SKPaint
        {
            Color = color,
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        canvas.DrawCircle(centerX, bulbY, bulbRadius, bulbPaint);

        // Draw Mercury (Level)
        float levelHeight = tubeHeight * pct;
        using var levelPaint = new SKPaint
        {
            Shader = SKShader.CreateLinearGradient(
                new SKPoint(centerX, bulbY),
                new SKPoint(centerX, tubeTopY),
                new SKColor[] { color, color.WithAlpha(150) },
                null,
                SKShaderTileMode.Clamp),
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        canvas.DrawRoundRect(centerX - tubeWidth / 3, bulbY - levelHeight - bulbRadius / 2, tubeWidth / 1.5f, levelHeight + bulbRadius / 2, tubeWidth / 4, tubeWidth / 4, levelPaint);

        // Glass highlight
        using var highlightPaint = new SKPaint
        {
            Color = SKColors.White.WithAlpha(50),
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
        canvas.DrawRoundRect(centerX - tubeWidth / 4, tubeTopY + 5, tubeWidth / 6, tubeHeight / 3, tubeWidth / 8, tubeWidth / 8, highlightPaint);
    }

    private SKColor GetTemperatureColor(double temp)
    {
        if (temp <= 18) return SKColor.Parse("#00aaff");
        if (temp < 25) return SKColor.Parse("#00ddcc");
        if (temp <= 33) return SKColor.Parse("#00e87a");
        if (temp < 38) return SKColor.Parse("#ffbb00");
        return SKColor.Parse("#ff4422");
    }
}
