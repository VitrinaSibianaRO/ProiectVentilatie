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
        float padding = 15f; 
        float radius = Math.Min(dirtyRect.Width, dirtyRect.Height) / 2 - padding;

        if (radius <= 0) return;

        // 1. Draw 360 Dotted Background Circle
        canvas.StrokeColor = Color.FromArgb("#555555");
        canvas.StrokeSize = 2f;
        canvas.StrokeDashPattern = new float[] { 2f, 6f };
        
        canvas.DrawCircle(centerX, centerY, radius);

        // Reset dash pattern
        canvas.StrokeDashPattern = null;

        // 2. Calculate Progress
        double range = _gauge.MaxValue - _gauge.MinValue;
        double fill = Math.Max(0, Math.Min(range, _gauge.Value - _gauge.MinValue));
        double percentage = range > 0 ? fill / range : 0;

        float startAngle = 135f; // Start from bottom left
        float totalSweep = 270f;
        float strokeThickness = 6f; // Thinner, solid elegant line

        // 3. Draw Progress Arc
        if (percentage > 0)
        {
            float currentEndAngle = startAngle + (totalSweep * (float)percentage);
            
            PathF progressPath = new PathF();
            if (percentage >= 0.999f)
            {
                progressPath.AddArc(centerX - radius, centerY - radius, radius * 2, radius * 2, startAngle, startAngle + totalSweep, true);
            }
            else
            {
                progressPath.AddArc(centerX - radius, centerY - radius, radius * 2, radius * 2, startAngle, currentEndAngle % 360, true);
            }

            canvas.StrokeColor = _gauge.ProgressColor;
            canvas.StrokeSize = strokeThickness;
            canvas.StrokeLineCap = LineCap.Round;
            canvas.DrawPath(progressPath);
            
            // 4. Draw Knob at end of progress
            double endRad = (currentEndAngle % 360) * Math.PI / 180.0;
            float knobX = centerX + (float)(radius * Math.Cos(endRad));
            float knobY = centerY + (float)(radius * Math.Sin(endRad));

            canvas.FillColor = Color.FromArgb("#121212"); // Background color to look hollow
            canvas.DrawCircle(knobX, knobY, 5f);
            canvas.StrokeColor = Colors.White;
            canvas.StrokeSize = 2f;
            canvas.DrawCircle(knobX, knobY, 5f);
        }

        // 5. Draw Texts
        string valueStr = $"{_gauge.Value:F1}"; // 1 decimal, e.g. 21.5
        canvas.FontColor = Colors.White;
        canvas.FontSize = 40; 
        canvas.Font = Microsoft.Maui.Graphics.Font.Default;
        
        // We use GetStringSize to accurately place the unit relative to the main value
        SizeF valueSize = canvas.GetStringSize(valueStr, Microsoft.Maui.Graphics.Font.Default, 40);
        
        // Draw Value
        canvas.DrawString(valueStr, 0, centerY - (valueSize.Height / 2), dirtyRect.Width, valueSize.Height, HorizontalAlignment.Center, VerticalAlignment.Center);
        
        // Draw Unit (top right of the value)
        canvas.FontSize = 16;
        canvas.FontColor = Colors.White;
        float unitX = centerX + (valueSize.Width / 2) + 2;
        canvas.DrawString(_gauge.Units, unitX, centerY - (valueSize.Height / 2), 50, 20, HorizontalAlignment.Left, VerticalAlignment.Top);

        // Draw Short Title
        canvas.FontColor = Colors.LightGray;
        canvas.FontSize = 12;
        canvas.DrawString(_gauge.Title, 0, centerY - (valueSize.Height / 2) - 25, dirtyRect.Width, 20, HorizontalAlignment.Center, VerticalAlignment.Bottom);
    }
}
