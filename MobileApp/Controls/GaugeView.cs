using Microsoft.Maui.Controls;
using Microsoft.Maui.Graphics;

namespace ProiectVentilatie.Mobile.Controls;

public class GaugeView : GraphicsView
{
    public static readonly BindableProperty ValueProperty = BindableProperty.Create(
        nameof(Value), typeof(double), typeof(GaugeView), 0.0, propertyChanged: OnGaugePropertyChanged);

    public static readonly BindableProperty MinValueProperty = BindableProperty.Create(
        nameof(MinValue), typeof(double), typeof(GaugeView), 0.0, propertyChanged: OnGaugePropertyChanged);

    public static readonly BindableProperty MaxValueProperty = BindableProperty.Create(
        nameof(MaxValue), typeof(double), typeof(GaugeView), 100.0, propertyChanged: OnGaugePropertyChanged);

    public static readonly BindableProperty UnitsProperty = BindableProperty.Create(
        nameof(Units), typeof(string), typeof(GaugeView), string.Empty, propertyChanged: OnGaugePropertyChanged);

    public static readonly BindableProperty TitleProperty = BindableProperty.Create(
        nameof(Title), typeof(string), typeof(GaugeView), string.Empty, propertyChanged: OnGaugePropertyChanged);

    public static readonly BindableProperty ProgressColorProperty = BindableProperty.Create(
        nameof(ProgressColor), typeof(Color), typeof(GaugeView), Colors.Blue, propertyChanged: OnGaugePropertyChanged);

    public double Value
    {
        get => (double)GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }
    public double MinValue { get => (double)GetValue(MinValueProperty); set => SetValue(MinValueProperty, value); }
    public double MaxValue { get => (double)GetValue(MaxValueProperty); set => SetValue(MaxValueProperty, value); }
    public string Units { get => (string)GetValue(UnitsProperty); set => SetValue(UnitsProperty, value); }
    public string Title { get => (string)GetValue(TitleProperty); set => SetValue(TitleProperty, value); }
    public Color ProgressColor { get => (Color)GetValue(ProgressColorProperty); set => SetValue(ProgressColorProperty, value); }

    private readonly GaugeDrawable _drawable;

    public GaugeView()
    {
        _drawable = new GaugeDrawable(this);
        Drawable = _drawable;
    }

    private static void OnGaugePropertyChanged(BindableObject bindable, object oldValue, object newValue)
    {
        if (bindable is GaugeView gaugeView)
        {
            gaugeView.Invalidate();
        }
    }
}

public class GaugeDrawable : IDrawable
{
    private readonly GaugeView _gauge;

    public GaugeDrawable(GaugeView gauge)
    {
        _gauge = gauge;
    }

    public void Draw(ICanvas canvas, RectF dirtyRect)
    {
        float centerX = dirtyRect.Width / 2;
        float centerY = dirtyRect.Height / 2;
        float padding = 10f; 
        float radius = Math.Min(dirtyRect.Width, dirtyRect.Height) / 2 - padding;

        if (radius <= 0) return;

        // 1. Draw Background Track (Subtle)
        canvas.StrokeColor = Color.FromArgb("#222222");
        canvas.StrokeSize = radius * 0.08f;
        canvas.StrokeLineCap = LineCap.Round;
        float startAngle = 135f;
        float totalSweep = 270f;
        
        PathF trackPath = new PathF();
        trackPath.AddArc(centerX - radius, centerY - radius, radius * 2, radius * 2, startAngle, (startAngle + totalSweep) % 360, true);
        canvas.DrawPath(trackPath);

        // 2. Draw 360 Dotted Background Circle (Decorative)
        canvas.StrokeColor = Color.FromArgb("#444444");
        canvas.StrokeSize = 1.5f;
        canvas.StrokeDashPattern = new float[] { 2f, 6f };
        canvas.DrawCircle(centerX, centerY, radius + 5);
        canvas.StrokeDashPattern = null;

        // 3. Calculate Progress
        double range = _gauge.MaxValue - _gauge.MinValue;
        double fill = Math.Max(0, Math.Min(range, _gauge.Value - _gauge.MinValue));
        double percentage = range > 0 ? fill / range : 0;

        // 4. Draw Progress Arc
        if (percentage > 0)
        {
            float sweepAngle = totalSweep * (float)percentage;
            float currentEndAngle = startAngle + sweepAngle;
            
            PathF progressPath = new PathF();
            progressPath.AddArc(centerX - radius, centerY - radius, radius * 2, radius * 2, startAngle, currentEndAngle % 360, true);

            canvas.StrokeColor = _gauge.ProgressColor;
            canvas.StrokeSize = radius * 0.1f;
            canvas.StrokeLineCap = LineCap.Round;
            canvas.DrawPath(progressPath);
            
            // 5. Draw Knob at end of progress
            double endRad = (currentEndAngle % 360) * Math.PI / 180.0;
            float knobX = centerX + (float)(radius * Math.Cos(endRad));
            float knobY = centerY + (float)(radius * Math.Sin(endRad));

            canvas.FillColor = Color.FromArgb("#121212");
            canvas.FillCircle(knobX, knobY, radius * 0.06f);
            canvas.StrokeColor = Colors.White;
            canvas.StrokeSize = 2f;
            canvas.DrawCircle(knobX, knobY, radius * 0.06f);
        }

        // 6. Draw Texts (Using relative font sizes)
        string valueStr = $"{_gauge.Value:F1}";
        float mainFontSize = radius * 0.55f;
        float unitFontSize = radius * 0.22f;
        float titleFontSize = radius * 0.18f;

        canvas.FontColor = Colors.White;
        canvas.Font = Microsoft.Maui.Graphics.Font.Default;

        // Measurement for alignment
        canvas.FontSize = mainFontSize;
        SizeF valueSize = canvas.GetStringSize(valueStr, Microsoft.Maui.Graphics.Font.Default, mainFontSize);
        
        // Value (Centered in the control)
        // We use a larger rectangle to avoid clipping on Android
        RectF valueRect = new RectF(0, centerY - (valueSize.Height / 2) - 5, dirtyRect.Width, valueSize.Height + 10);
        canvas.DrawString(valueStr, valueRect, HorizontalAlignment.Center, VerticalAlignment.Center);
        
        // Unit (Positioned relative to value)
        canvas.FontSize = unitFontSize;
        float unitX = centerX + (valueSize.Width / 2) + 4;
        RectF unitRect = new RectF(unitX, centerY - (valueSize.Height / 2) + 5, 60, 30);
        canvas.DrawString(_gauge.Units, unitRect, HorizontalAlignment.Left, VerticalAlignment.Top);

        // Title (Above the value)
        canvas.FontColor = Color.FromArgb("#BBBBBB");
        canvas.FontSize = titleFontSize;
        RectF titleRect = new RectF(0, centerY - (valueSize.Height / 2) - titleFontSize - 8, dirtyRect.Width, titleFontSize + 10);
        canvas.DrawString(_gauge.Title, titleRect, HorizontalAlignment.Center, VerticalAlignment.Bottom);
    }
}
