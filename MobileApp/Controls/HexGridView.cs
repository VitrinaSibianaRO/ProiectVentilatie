using SkiaSharp;
using SkiaSharp.Views.Maui;
using SkiaSharp.Views.Maui.Controls;

namespace ProiectVentilatie.Mobile.Controls;

/// <summary>
/// Decorative hex grid background — 36+ hexagons with subtle cyan stroke.
/// Renders once (static), no animation needed.
/// </summary>
public class HexGridView : SKCanvasView
{
    protected override void OnPaintSurface(SKPaintSurfaceEventArgs e)
    {
        var canvas = e.Surface.Canvas;
        canvas.Clear(SKColors.Transparent);

        float w = e.Info.Width;
        float h = e.Info.Height;
        float size = 44f;
        float hexW = (float)(Math.Sqrt(3) * size);
        float hexH = 2f * size;

        using var paint = new SKPaint
        {
            Style = SKPaintStyle.Stroke,
            StrokeWidth = 1f,
            Color = SKColor.Parse("#00e6ff").WithAlpha(18),
            IsAntialias = true
        };

        int row = 0;
        for (float y = -hexH * 0.25f; y < h + hexH; y += hexH * 0.75f, row++)
        {
            float xOffset = (row % 2 == 0) ? 0 : hexW / 2f;
            for (float x = -hexW + xOffset; x < w + hexW; x += hexW)
                DrawHex(canvas, x, y, size, paint);
        }
    }

    private static void DrawHex(SKCanvas canvas, float cx, float cy, float size, SKPaint paint)
    {
        using var path = new SKPath();
        for (int i = 0; i < 6; i++)
        {
            double angle = Math.PI / 180.0 * (60 * i - 30);
            float px = cx + (float)(size * Math.Cos(angle));
            float py = cy + (float)(size * Math.Sin(angle));
            if (i == 0) path.MoveTo(px, py);
            else path.LineTo(px, py);
        }
        path.Close();
        canvas.DrawPath(path, paint);
    }
}
