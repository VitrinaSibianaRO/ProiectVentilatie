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
        var center = new PointF(dirtyRect.Width / 2, dirtyRect.Height / 2);
        var radius = Math.Min(dirtyRect.Width, dirtyRect.Height) / 2 - 15;

        if (radius <= 0) return;

        // Microsoft.Maui.Graphics: 0 is East, positive is clockwise.
        float startAngle = 135f;
        float endAngle = 45f;
        float totalSweep = 270f;

        // Draw background arc
        PathF backgroundPath = new PathF();
        backgroundPath.AddArc(center.X - radius, center.Y - radius, radius * 2, radius * 2, startAngle, endAngle, true);
        canvas.StrokeColor = Color.FromArgb("#333333");
        canvas.StrokeSize = 12;
        canvas.StrokeLineCap = LineCap.Round;
        canvas.DrawPath(backgroundPath);

        // Calculate progress
        double range = _gauge.MaxValue - _gauge.MinValue;
        double fill = Math.Max(0, Math.Min(range, _gauge.Value - _gauge.MinValue));
        double percentage = range > 0 ? fill / range : 0;

        // Draw foreground arc
        if (percentage > 0)
        {
            float currentEndAngle = startAngle + (totalSweep * (float)percentage);
            currentEndAngle = currentEndAngle % 360;

            PathF progressPath = new PathF();
            
            // Handle edge case where percentage is 1.0 (draw full sweep)
            if (percentage >= 0.999f)
            {
                progressPath.AddArc(center.X - radius, center.Y - radius, radius * 2, radius * 2, startAngle, endAngle, true);
            }
            else
            {
                progressPath.AddArc(center.X - radius, center.Y - radius, radius * 2, radius * 2, startAngle, currentEndAngle, true);
            }

            canvas.StrokeColor = _gauge.ProgressColor;
            canvas.StrokeSize = 12;
            canvas.StrokeLineCap = LineCap.Round;
            canvas.DrawPath(progressPath);
        }

        // Draw texts
        canvas.FontColor = Colors.White;
        canvas.FontSize = 20;
        canvas.Font = Microsoft.Maui.Graphics.Font.DefaultBold;
        
        string valueStr = $"{_gauge.Value:F1}{_gauge.Units}";
        canvas.DrawString(valueStr, 0, 0, dirtyRect.Width, dirtyRect.Height, HorizontalAlignment.Center, VerticalAlignment.Center);

        canvas.FontSize = 12;
        canvas.FontColor = Colors.LightGray;
        canvas.Font = Microsoft.Maui.Graphics.Font.Default;
        canvas.DrawString(_gauge.Title, 0, 25, dirtyRect.Width, dirtyRect.Height, HorizontalAlignment.Center, VerticalAlignment.Center);
    }
}
