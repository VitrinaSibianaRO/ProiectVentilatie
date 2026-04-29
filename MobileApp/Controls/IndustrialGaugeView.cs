using SkiaSharp;
using SkiaSharp.Views.Maui;
using SkiaSharp.Views.Maui.Controls;

namespace ProiectVentilatie.Mobile.Controls;

public class IndustrialGaugeView : SKCanvasView
{
    public static readonly BindableProperty ValueProperty = BindableProperty.Create(
        nameof(Value), typeof(double), typeof(IndustrialGaugeView), 0.0, propertyChanged: (b, o, n) => ((IndustrialGaugeView)b).InvalidateSurface());

    public static readonly BindableProperty MinValueProperty = BindableProperty.Create(
        nameof(MinValue), typeof(double), typeof(IndustrialGaugeView), 0.0);

    public static readonly BindableProperty MaxValueProperty = BindableProperty.Create(
        nameof(MaxValue), typeof(double), typeof(IndustrialGaugeView), 100.0);

    public static readonly BindableProperty TitleProperty = BindableProperty.Create(
        nameof(Title), typeof(string), typeof(IndustrialGaugeView), string.Empty);

    public static readonly BindableProperty UnitsProperty = BindableProperty.Create(
        nameof(Units), typeof(string), typeof(IndustrialGaugeView), string.Empty);

    public static readonly BindableProperty AccentColorProperty = BindableProperty.Create(
        nameof(AccentColor), typeof(Color), typeof(IndustrialGaugeView), Colors.Cyan);

    public double Value { get => (double)GetValue(ValueProperty); set => SetValue(ValueProperty, value); }
    public double MinValue { get => (double)GetValue(MinValueProperty); set => SetValue(MinValueProperty, value); }
    public double MaxValue { get => (double)GetValue(MaxValueProperty); set => SetValue(MaxValueProperty, value); }
    public string Title { get => (string)GetValue(TitleProperty); set => SetValue(TitleProperty, value); }
    public string Units { get => (string)GetValue(UnitsProperty); set => SetValue(UnitsProperty, value); }
    public Color AccentColor { get => (Color)GetValue(AccentColorProperty); set => SetValue(AccentColorProperty, value); }

    protected override void OnPaintSurface(SKPaintSurfaceEventArgs e)
    {
        var canvas = e.Surface.Canvas;
        var info = e.Info;

        canvas.Clear();

        float width = info.Width;
        float height = info.Height;
        float centerX = width / 2;
        float centerY = height / 2;
        float radius = Math.Min(width, height) / 2 * 0.85f;

        // Draw Background Ring
        using var bgPaint = new SKPaint
        {
            Style = SKPaintStyle.Stroke,
            Color = new SKColor(0, 230, 255, 30),
            StrokeWidth = radius * 0.1f,
            IsAntialias = true
        };
        canvas.DrawCircle(centerX, centerY, radius, bgPaint);

        // Draw Value Arc
        float startAngle = 135;
        float sweepAngle = 270;
        float progress = (float)((Value - MinValue) / (MaxValue - MinValue));
        progress = Math.Clamp(progress, 0, 1);

        using var arcPaint = new SKPaint
        {
            Style = SKPaintStyle.Stroke,
            StrokeWidth = radius * 0.12f,
            StrokeCap = SKStrokeCap.Round,
            IsAntialias = true,
            Shader = SKShader.CreateSweepGradient(
                new SKPoint(centerX, centerY),
                new SKColor[] { SKColors.Cyan, SKColors.Cyan, SKColors.Transparent },
                new float[] { 0, progress * 0.75f, 1 })
        };
        
        // Correcting arc path
        using var path = new SKPath();
        path.AddArc(new SKRect(centerX - radius, centerY - radius, centerX + radius, centerY + radius), startAngle, sweepAngle * progress);
        canvas.DrawPath(path, arcPaint);

        // Draw Title
        using var textPaint = new SKPaint
        {
            Color = SKColors.White.WithAlpha(150),
            TextSize = radius * 0.15f,
            TextAlign = SKTextAlign.Center,
            Typeface = SKTypeface.FromFamilyName("Rajdhani"),
            IsAntialias = true
        };
        canvas.DrawText(Title.ToUpper(), centerX, centerY - radius * 0.4f, textPaint);

        // Draw Value
        using var valPaint = new SKPaint
        {
            Color = SKColors.White,
            TextSize = radius * 0.5f,
            TextAlign = SKTextAlign.Center,
            Typeface = SKTypeface.FromFamilyName("Share Tech Mono"),
            IsAntialias = true,
            FakeBoldText = true
        };
        canvas.DrawText($"{Value:F1}", centerX, centerY + radius * 0.15f, valPaint);

        // Draw Units
        using var unitPaint = new SKPaint
        {
            Color = SKColors.Cyan,
            TextSize = radius * 0.2f,
            TextAlign = SKTextAlign.Center,
            Typeface = SKTypeface.FromFamilyName("Rajdhani"),
            IsAntialias = true
        };
        canvas.DrawText(Units, centerX, centerY + radius * 0.45f, unitPaint);
    }
}
