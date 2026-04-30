using SkiaSharp;
using SkiaSharp.Views.Maui;
using SkiaSharp.Views.Maui.Controls;

namespace ProiectVentilatie.Mobile.Controls;

/// <summary>
/// Animated showcase hero banner — 3 cycling slides: Brand / Live Data / Network.
/// Matches the VitrinaHero React component from the Industrial Dashboard design.
/// ViewBox reference: 0 0 375 200.
/// </summary>
public class VitrinaHeroView : SKCanvasView
{
    // ── Bindable properties ───────────────────────────────────────────────────

    public static readonly BindableProperty LeftTempProperty = BindableProperty.Create(
        nameof(LeftTemp), typeof(float), typeof(VitrinaHeroView), 0f, propertyChanged: Invalidate);
    public static readonly BindableProperty LeftHumProperty = BindableProperty.Create(
        nameof(LeftHum), typeof(float), typeof(VitrinaHeroView), 0f, propertyChanged: Invalidate);
    public static readonly BindableProperty LeftRelayProperty = BindableProperty.Create(
        nameof(LeftRelay), typeof(bool), typeof(VitrinaHeroView), false, propertyChanged: Invalidate);
    public static readonly BindableProperty LeftOverrideProperty = BindableProperty.Create(
        nameof(LeftOverride), typeof(bool), typeof(VitrinaHeroView), false, propertyChanged: Invalidate);

    public static readonly BindableProperty RightTempProperty = BindableProperty.Create(
        nameof(RightTemp), typeof(float), typeof(VitrinaHeroView), 0f, propertyChanged: Invalidate);
    public static readonly BindableProperty RightHumProperty = BindableProperty.Create(
        nameof(RightHum), typeof(float), typeof(VitrinaHeroView), 0f, propertyChanged: Invalidate);
    public static readonly BindableProperty RightRelayProperty = BindableProperty.Create(
        nameof(RightRelay), typeof(bool), typeof(VitrinaHeroView), false, propertyChanged: Invalidate);
    public static readonly BindableProperty RightOverrideProperty = BindableProperty.Create(
        nameof(RightOverride), typeof(bool), typeof(VitrinaHeroView), false, propertyChanged: Invalidate);

    public static readonly BindableProperty FwBuildProperty = BindableProperty.Create(
        nameof(FwBuild), typeof(int), typeof(VitrinaHeroView), 0, propertyChanged: Invalidate);
    public static readonly BindableProperty UptimeSecProperty = BindableProperty.Create(
        nameof(UptimeSec), typeof(long), typeof(VitrinaHeroView), 0L, propertyChanged: Invalidate);
    public static readonly BindableProperty HeapBytesProperty = BindableProperty.Create(
        nameof(HeapBytes), typeof(int), typeof(VitrinaHeroView), 0, propertyChanged: Invalidate);
    public static readonly BindableProperty IsOnlineProperty = BindableProperty.Create(
        nameof(IsOnline), typeof(bool), typeof(VitrinaHeroView), false, propertyChanged: Invalidate);

    public float LeftTemp    { get => (float)GetValue(LeftTempProperty);    set => SetValue(LeftTempProperty,    value); }
    public float LeftHum     { get => (float)GetValue(LeftHumProperty);     set => SetValue(LeftHumProperty,     value); }
    public bool  LeftRelay   { get => (bool)GetValue(LeftRelayProperty);    set => SetValue(LeftRelayProperty,   value); }
    public bool  LeftOverride{ get => (bool)GetValue(LeftOverrideProperty); set => SetValue(LeftOverrideProperty,value); }
    public float RightTemp   { get => (float)GetValue(RightTempProperty);   set => SetValue(RightTempProperty,   value); }
    public float RightHum    { get => (float)GetValue(RightHumProperty);    set => SetValue(RightHumProperty,    value); }
    public bool  RightRelay  { get => (bool)GetValue(RightRelayProperty);   set => SetValue(RightRelayProperty,  value); }
    public bool  RightOverride{get => (bool)GetValue(RightOverrideProperty);set => SetValue(RightOverrideProperty,value);}
    public int   FwBuild     { get => (int)GetValue(FwBuildProperty);       set => SetValue(FwBuildProperty,     value); }
    public long  UptimeSec   { get => (long)GetValue(UptimeSecProperty);    set => SetValue(UptimeSecProperty,   value); }
    public int   HeapBytes   { get => (int)GetValue(HeapBytesProperty);     set => SetValue(HeapBytesProperty,   value); }
    public bool  IsOnline    { get => (bool)GetValue(IsOnlineProperty);     set => SetValue(IsOnlineProperty,    value); }

    private static void Invalidate(BindableObject b, object o, object n) => ((VitrinaHeroView)b).InvalidateSurface();

    // ── Animation state ───────────────────────────────────────────────────────

    private int _slideIdx = 0;
    private float _slideOpacity = 1f;
    private bool _isFading = false;
    private double _time = 0;         // seconds elapsed, drives all sine-based pulses
    private double _waveOffset = 0;   // drives wave scroll on slide 0
    private IDispatcherTimer? _timer;

    // 4.5s per slide, 0.65s fade-out → change → fade-in
    private const double SlideDuration = 4.5;
    private const double FadeDuration  = 0.65;
    private double _sinceLastSlide = 0;
    private bool _fadePhase = false;   // false=fade-out pending, true=faded-in

    // ── Fonts ─────────────────────────────────────────────────────────────────

    private static SKTypeface? _tfRajdhani;
    private static SKTypeface? _tfMono;
    private static bool _fontsTriedLoad = false;

    private static void EnsureFonts()
    {
        if (_fontsTriedLoad) return;
        _fontsTriedLoad = true;
        try
        {
            using var s1 = FileSystem.OpenAppPackageFileAsync("Rajdhani-Bold.ttf").GetAwaiter().GetResult();
            _tfRajdhani = SKTypeface.FromStream(s1);
        }
        catch { _tfRajdhani = SKTypeface.FromFamilyName("sans-serif", SKFontStyle.Bold); }
        try
        {
            using var s2 = FileSystem.OpenAppPackageFileAsync("ShareTechMono-Regular.ttf").GetAwaiter().GetResult();
            _tfMono = SKTypeface.FromStream(s2);
        }
        catch { _tfMono = SKTypeface.FromFamilyName("monospace"); }
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    protected override void OnHandlerChanged()
    {
        base.OnHandlerChanged();
        if (Handler != null)
            StartTimer();
        else
            StopTimer();
    }

    private void StartTimer()
    {
        if (_timer != null) return;
        _timer = Application.Current?.Dispatcher.CreateTimer();
        if (_timer == null) return;
        _timer.Interval = TimeSpan.FromMilliseconds(16); // ~60 fps
        _timer.Tick += OnTick;
        _timer.Start();
    }

    private void StopTimer()
    {
        _timer?.Stop();
        _timer = null;
    }

    private void OnTick(object? sender, EventArgs e)
    {
        const double dt = 0.016;
        _time += dt;
        _waveOffset += dt * 0.60; // wave speed: ~0.6 units/s (matches 3.5s period)
        if (_waveOffset > 1.0) _waveOffset -= 1.0;

        _sinceLastSlide += dt;
        if (!_isFading && _sinceLastSlide >= SlideDuration - FadeDuration)
        {
            _isFading = true;
        }
        if (_isFading)
        {
            if (!_fadePhase)
            {
                // Fading out
                _slideOpacity = (float)Math.Max(0, 1.0 - (_sinceLastSlide - (SlideDuration - FadeDuration)) / FadeDuration);
                if (_slideOpacity <= 0)
                {
                    _slideIdx = (_slideIdx + 1) % 3;
                    _slideOpacity = 0;
                    _fadePhase = true;
                }
            }
            else
            {
                // Fading in
                _slideOpacity = (float)Math.Min(1, (_sinceLastSlide - SlideDuration + FadeDuration) / FadeDuration);
                if (_slideOpacity >= 1)
                {
                    _slideOpacity = 1;
                    _isFading = false;
                    _fadePhase = false;
                    _sinceLastSlide = 0;
                }
            }
        }

        InvalidateSurface();
    }

    // ── Paint ─────────────────────────────────────────────────────────────────

    protected override void OnPaintSurface(SKPaintSurfaceEventArgs e)
    {
        EnsureFonts();
        var canvas = e.Surface.Canvas;
        var info   = e.Info;
        canvas.Clear();

        // Scale from 375×200 design coords
        float sx = info.Width  / 375f;
        float sy = info.Height / 200f;
        canvas.Scale(sx, sy);

        float t = (float)_time;

        // ── Helpers ──────────────────────────────────────────────────────────
        float GlowPulse(float period, float phase = 0f)
            => 0.5f + 0.5f * MathF.Sin((t + phase) * (2 * MathF.PI / period));

        using var p = new SKPaint { IsAntialias = true };

        // ── Outer metallic frame ──────────────────────────────────────────────
        var outerRect = new SKRoundRect(new SKRect(1, 1, 374, 199), 11, 11);
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0x07, 0x10, 0x1e);
        canvas.DrawRoundRect(outerRect, p);

        p.Style = SKPaintStyle.Stroke;
        p.StrokeWidth = 1.5f;
        p.Color = new SKColor(0x0e, 0x20, 0x35);
        canvas.DrawRoundRect(outerRect, p);

        // Animated border glow
        float glowA = GlowPulse(3.5f) * 0.65f;
        p.Style = SKPaintStyle.Stroke;
        p.StrokeWidth = 1.2f;
        p.Shader = SKShader.CreateLinearGradient(
            new SKPoint(0, 0), new SKPoint(375, 200),
            new[] { new SKColor(0, 230, 255, (byte)(180 * glowA)),
                    new SKColor(0, 120, 220, (byte)(76  * glowA)),
                    new SKColor(0, 230, 255, (byte)(180 * glowA)) },
            new float[] { 0f, 0.4f, 1f }, SKShaderTileMode.Clamp);
        canvas.DrawRoundRect(outerRect, p);
        p.Shader = null;

        // ── Inner bezel ───────────────────────────────────────────────────────
        var bezelRect = new SKRoundRect(new SKRect(7, 7, 368, 141), 7, 7);
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0x04, 0x0c, 0x18);
        canvas.DrawRoundRect(bezelRect, p);
        p.Style = SKPaintStyle.Stroke;
        p.StrokeWidth = 1f;
        p.Color = new SKColor(0x0c, 0x1c, 0x2c);
        canvas.DrawRoundRect(bezelRect, p);

        // ── Screen background ─────────────────────────────────────────────────
        var screenRect = new SKRect(11, 11, 364, 137);
        var screenRR   = new SKRoundRect(screenRect, 5, 5);
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0x02, 0x08, 0x10);
        canvas.DrawRoundRect(screenRR, p);

        // ── Screen content (clipped, with fade opacity) ───────────────────────
        canvas.Save();
        canvas.ClipRoundRect(screenRR);
        p.Color = p.Color.WithAlpha((byte)(_slideOpacity * 255));

        canvas.Save();
        canvas.Translate(11, 11); // slide content origin
        switch (_slideIdx)
        {
            case 0: DrawSlide0(canvas, p, t); break;
            case 1: DrawSlide1(canvas, p, t); break;
            case 2: DrawSlide2(canvas, p, t); break;
        }

        // CRT scanlines
        using var scanPaint = new SKPaint { Color = new SKColor(0, 0, 0, 33), Style = SKPaintStyle.Fill };
        for (int i = 0; i < 32; i++)
            canvas.DrawRect(0, i * 4, 353, 0.7f, scanPaint);

        canvas.Restore(); // slide translate
        canvas.Restore(); // clip

        // Screen border rim
        p.Style = SKPaintStyle.Stroke;
        p.StrokeWidth = 0.5f;
        p.Color = new SKColor(0, 230, 255, 46);
        canvas.DrawRoundRect(screenRR, p);

        // ── LED strip top ─────────────────────────────────────────────────────
        float ledA = 0.6f + 0.4f * GlowPulse(2.5f);
        DrawLedStrip(canvas, 28, 7, 319, 2.5f, ledA);

        // ── Bottom shelf ──────────────────────────────────────────────────────
        var shelfRect = new SKRoundRect(new SKRect(7, 145, 368, 195), 7, 7);
        using var shelfPaint = new SKPaint
        {
            Style = SKPaintStyle.Fill,
            Shader = SKShader.CreateLinearGradient(
                new SKPoint(187, 145), new SKPoint(187, 195),
                new[] { new SKColor(0x0d, 0x20, 0x35), new SKColor(0x06, 0x0e, 0x1a) },
                null, SKShaderTileMode.Clamp),
            IsAntialias = true
        };
        canvas.DrawRoundRect(shelfRect, shelfPaint);
        p.Style = SKPaintStyle.Stroke;
        p.StrokeWidth = 1f;
        p.Color = new SKColor(0x0c, 0x1c, 0x2c);
        canvas.DrawRoundRect(shelfRect, p);

        // LED strip bottom of shelf
        float ledA2 = 0.45f * (0.6f + 0.4f * GlowPulse(3.2f));
        DrawLedStrip(canvas, 28, 192.5f, 319, 1.5f, ledA2);

        // ── Slide indicator dots ──────────────────────────────────────────────
        for (int i = 0; i < 3; i++)
        {
            float cx = 176.5f + (i - 1) * 13f;
            bool active = i == _slideIdx;
            float r = active ? 4f : 2.5f;
            p.Style = SKPaintStyle.Fill;
            p.Color = active ? new SKColor(0, 230, 255, 255) : new SKColor(0, 230, 255, 56);
            canvas.DrawCircle(cx, 162, r, p);
        }

        // ── Zone relay status in shelf ────────────────────────────────────────
        DrawZoneIndicator(canvas, 22, 162, "STG", LeftRelay,  t, GlowPulse(2.0f),  2.0f);
        DrawZoneIndicatorRight(canvas, 298, 162, "DRT", RightRelay, t, GlowPulse(2.3f, 0.3f));

        // ── Side LED strips ───────────────────────────────────────────────────
        float sideA1 = 0.28f * (0.5f + 0.5f * GlowPulse(3.0f));
        float sideA2 = 0.28f * (0.5f + 0.5f * GlowPulse(3.0f, 0.6f));
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0, 230, 255, (byte)(sideA1 * 255));
        canvas.DrawRoundRect(new SKRoundRect(new SKRect(3.5f, 16, 5, 165), 0.75f, 0.75f), p);
        p.Color = new SKColor(0, 230, 255, (byte)(sideA2 * 255));
        canvas.DrawRoundRect(new SKRoundRect(new SKRect(370, 16, 371.5f, 165), 0.75f, 0.75f), p);

        // ── Corner accent LEDs ────────────────────────────────────────────────
        (float x, float y, float period, float phase)[] corners =
        {
            (7, 7,     1.8f, 0.0f),
            (368, 7,   2.0f, 0.25f),
            (7, 190,   2.2f, 0.5f),
            (368, 190, 2.4f, 0.75f)
        };
        foreach (var (cx, cy, period, phase) in corners)
        {
            float ca = 0.4f + 0.6f * GlowPulse(period, phase);
            p.Style = SKPaintStyle.Fill;
            p.Color = new SKColor(0, 230, 255, 31);
            canvas.DrawCircle(cx, cy, 4, p);
            p.Color = new SKColor(0, 230, 255, (byte)(178 * ca));
            canvas.DrawCircle(cx, cy, 2.5f, p);
        }
    }

    // ── SLIDE 0 — Brand ───────────────────────────────────────────────────────

    private void DrawSlide0(SKCanvas canvas, SKPaint p, float t)
    {
        // Background
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0x02, 0x08, 0x10);
        canvas.DrawRect(0, 0, 353, 126, p);

        // Subtle grid lines
        using var gridPaint = new SKPaint { Color = new SKColor(0, 230, 255, 10), Style = SKPaintStyle.Stroke, StrokeWidth = 1 };
        for (int i = 0; i <= 7; i++) canvas.DrawLine(i * 51f, 0, i * 51f, 126, gridPaint);
        for (int i = 0; i <= 3; i++) canvas.DrawLine(0, i * 32f, 353, i * 32f, gridPaint);

        // Title "VITRINA"
        using var titlePaint = new SKPaint
        {
            Typeface = _tfRajdhani,
            TextSize = 26,
            TextAlign = SKTextAlign.Center,
            Color = new SKColor(255, 255, 255, 240),
            IsAntialias = true
        };
        using var blurFilter = SKMaskFilter.CreateBlur(SKBlurStyle.Normal, 4f);
        var glowPaint = titlePaint.Clone();
        glowPaint.Color = new SKColor(0, 230, 255, 120);
        glowPaint.MaskFilter = blurFilter;
        canvas.DrawText("VITRINA", 176.5f, 38, glowPaint);
        canvas.DrawText("VITRINA", 176.5f, 38, titlePaint);

        // Subtitle "SIBIANĂ"
        using var subPaint = new SKPaint
        {
            Typeface = _tfRajdhani,
            TextSize = 13,
            TextAlign = SKTextAlign.Center,
            Color = new SKColor(0, 230, 255, 240),
            IsAntialias = true,
            FakeBoldText = true
        };
        var subGlow = subPaint.Clone();
        subGlow.MaskFilter = SKMaskFilter.CreateBlur(SKBlurStyle.Normal, 3f);
        subGlow.Color = new SKColor(0, 230, 255, 90);
        canvas.DrawText("SIBIANA", 176.5f, 57, subGlow);
        canvas.DrawText("SIBIANA", 176.5f, 57, subPaint);

        // Divider line + center dot
        using var divPaint = new SKPaint { Color = new SKColor(0, 230, 255, 127), Style = SKPaintStyle.Stroke, StrokeWidth = 0.7f };
        canvas.DrawLine(60, 66, 293, 66, divPaint);
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0, 230, 255, 204);
        canvas.DrawCircle(176.5f, 66, 3, p);

        // Animated wave
        DrawWave(canvas, _waveOffset);

        // Tagline
        using var tagPaint = new SKPaint
        {
            Typeface = _tfMono,
            TextSize = 9,
            TextAlign = SKTextAlign.Center,
            Color = new SKColor(255, 255, 255, 89),
            IsAntialias = true
        };
        canvas.DrawText("SMART CONTROL v2.0  ESP32 + MAUI + HiveMQ", 176.5f, 116, tagPaint);

        // Corner brackets
        DrawCornerBrackets(canvas);
    }

    private void DrawWave(SKCanvas canvas, double offset)
    {
        canvas.Save();
        var clipRect = new SKRect(0, 68, 353, 108);
        canvas.ClipRect(clipRect);

        float ox = (float)(offset * 375); // wave X offset in pixels (375px period)
        float[] amplitudes = { 14f, 12f };
        SKColor[] colors = { new SKColor(0, 230, 255, 140), new SKColor(0, 180, 255, 64) };
        float[] widths = { 1.5f, 1.0f };
        float[] ys = { 90f, 96f };

        for (int w = 0; w < 2; w++)
        {
            using var wavePaint = new SKPaint { Color = colors[w], Style = SKPaintStyle.Stroke, StrokeWidth = widths[w], IsAntialias = true };
            var path = new SKPath();
            bool first = true;
            for (float x = -ox - 375; x <= 375; x += 2f)
            {
                float y = ys[w] + amplitudes[w] * MathF.Sin((x + ox) * (2 * MathF.PI / 75f));
                if (first) { path.MoveTo(x, y); first = false; }
                else path.LineTo(x, y);
            }
            canvas.DrawPath(path, wavePaint);
        }

        canvas.Restore();
    }

    private void DrawCornerBrackets(SKCanvas canvas)
    {
        (float x, float y, float sx, float sy)[] corners =
        {
            (10, 6, 1, 1), (343, 6, -1, 1), (10, 120, 1, -1), (343, 120, -1, -1)
        };
        using var bPaint = new SKPaint { Color = new SKColor(0, 230, 255, 178), Style = SKPaintStyle.Stroke, StrokeWidth = 1.5f };
        foreach (var (x, y, sx, sy) in corners)
        {
            canvas.DrawLine(x, y, x + sx * 14, y, bPaint);
            canvas.DrawLine(x, y, x, y + sy * 14, bPaint);
        }
    }

    // ── SLIDE 1 — Live Data ───────────────────────────────────────────────────

    private void DrawSlide1(SKCanvas canvas, SKPaint p, float t)
    {
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0x02, 0x08, 0x10);
        canvas.DrawRect(0, 0, 353, 126, p);

        // Header
        DrawMonoText(canvas, "DATE IN TIMP REAL", 176.5f, 14, 10, new SKColor(0, 230, 255, 140), SKTextAlign.Center);

        // Left zone card
        DrawZoneCard(canvas, p, t, 8, 18, 160, 98, "STANGA",
            LeftTemp, LeftHum, LeftRelay, LeftOverride, GetTempColor(LeftTemp));

        // Right zone card
        DrawZoneCard(canvas, p, t, 185, 18, 160, 98, "DREAPTA",
            RightTemp, RightHum, RightRelay, RightOverride, GetTempColor(RightTemp));

        // Center divider
        using var divPaint = new SKPaint { Color = new SKColor(0, 230, 255, 30), Style = SKPaintStyle.Stroke, StrokeWidth = 1, PathEffect = SKPathEffect.CreateDash(new float[] { 4, 3 }, 0) };
        canvas.DrawLine(176.5f, 22, 176.5f, 112, divPaint);
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0, 230, 255, 51);
        canvas.DrawCircle(176.5f, 67, 2, p);
    }

    private void DrawZoneCard(SKCanvas canvas, SKPaint p, float t,
        float x, float y, float w, float h, string label,
        float temp, float hum, bool relay, bool isOverride, SKColor tempColor)
    {
        float cx = x + w / 2f;

        // Card background
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0x00, 0x0f, 0x28, 216);
        canvas.DrawRoundRect(new SKRoundRect(new SKRect(x, y, x + w, y + h), 6, 6), p);
        p.Style = SKPaintStyle.Stroke;
        p.StrokeWidth = 1;
        p.Color = new SKColor(0, 230, 255, 51);
        canvas.DrawRoundRect(new SKRoundRect(new SKRect(x, y, x + w, y + h), 6, 6), p);

        // Zone label
        using var lblPaint = new SKPaint { Typeface = _tfRajdhani, TextSize = 10, TextAlign = SKTextAlign.Center, Color = new SKColor(255, 255, 255, 127), IsAntialias = true, FakeBoldText = true };
        canvas.DrawText(label, cx, y + 14, lblPaint);

        // Temperature value
        using var tmpPaint = new SKPaint { Typeface = _tfMono, TextSize = 24, TextAlign = SKTextAlign.Center, Color = tempColor, IsAntialias = true, FakeBoldText = true };
        canvas.DrawText($"{temp:0.0}°C", cx, y + 42, tmpPaint);

        // Temp label
        using var tlPaint = new SKPaint { Typeface = _tfRajdhani, TextSize = 10, TextAlign = SKTextAlign.Center, Color = tempColor.WithAlpha(191), IsAntialias = true };
        canvas.DrawText(GetTempLabel(temp), cx, y + 55, tlPaint);

        // Humidity
        using var humPaint = new SKPaint { Typeface = _tfMono, TextSize = 13, TextAlign = SKTextAlign.Center, Color = new SKColor(0x44, 0xaa, 0xff), IsAntialias = true };
        canvas.DrawText($"{hum:0.0}% RH", cx, y + 70, humPaint);

        // Relay indicator LED
        float pulseA = relay ? (0.5f + 0.5f * MathF.Sin(t * MathF.PI)) : 0f;
        float ledX = x + w * 0.32f;
        float ledY = y + h - 16f;
        p.Style = SKPaintStyle.Fill;
        p.Color = relay ? new SKColor(0, 232, 122).WithAlpha((byte)(100 + pulseA * 100)) : new SKColor(255, 68, 68, 100);
        canvas.DrawCircle(ledX, ledY, 4.5f, p);
        p.Color = relay ? new SKColor(0, 232, 122) : new SKColor(255, 68, 68);
        canvas.DrawCircle(ledX, ledY, 3.5f, p);

        // Relay text
        string statusText = relay ? "ON" : "OFF";
        string modeChar = isOverride ? " M" : " A";
        using var relPaint = new SKPaint { Typeface = _tfRajdhani, TextSize = 12, TextAlign = SKTextAlign.Left, Color = relay ? new SKColor(0, 232, 122) : new SKColor(255, 102, 68), IsAntialias = true, FakeBoldText = true };
        canvas.DrawText(statusText + modeChar, ledX + 9f, ledY + 4f, relPaint);
    }

    // ── SLIDE 2 — Network ─────────────────────────────────────────────────────

    private void DrawSlide2(SKCanvas canvas, SKPaint p, float t)
    {
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0x02, 0x08, 0x10);
        canvas.DrawRect(0, 0, 353, 126, p);

        DrawMonoText(canvas, "STATUS RETEA", 176.5f, 14, 10, new SKColor(0, 230, 255, 140), SKTextAlign.Center);

        // 3 nodes
        DrawNetNode(canvas, p, t, 55,    64, "ESP32",    IsOnline ? "ONLINE" : "OFFLINE",
            new SKColor(0, 230, 255), IsOnline ? new SKColor(0, 232, 122) : new SKColor(255, 102, 68), 2.2f, 0.0f);
        DrawNetNode(canvas, p, t, 176.5f,64, "HiveMQ",   "MQTT TLS",
            new SKColor(0, 170, 255), new SKColor(0, 170, 255), 2.6f, 0.4f, "8883");
        DrawNetNode(canvas, p, t, 298,   64, ".NET MAUI", "MOBILE",
            new SKColor(0, 204, 136), new SKColor(0, 221, 170), 3.0f, 0.8f, "Android");

        // Connection lines
        using var linePaint = new SKPaint { Style = SKPaintStyle.Stroke, StrokeWidth = 1.5f, IsAntialias = true,
            PathEffect = SKPathEffect.CreateDash(new float[] { 5, 3 }, 0) };
        linePaint.Color = new SKColor(0, 200, 255, 76);
        canvas.DrawLine(81, 64, 150.5f, 64, linePaint);
        linePaint.Color = new SKColor(0, 200, 140, 76);
        canvas.DrawLine(202.5f, 64, 272, 64, linePaint);

        // Travelling dots on left segment
        for (int i = 0; i < 3; i++)
        {
            float phase = (float)((t / 2.0 + i * 0.65 / 2.0) % 1.0);
            float dotX = 81 + phase * 69.5f;
            p.Style = SKPaintStyle.Fill;
            p.Color = new SKColor(0, 230, 255, (byte)(200 * (1 - Math.Abs(phase - 0.5f) * 2)));
            canvas.DrawCircle(dotX, 64, 2.5f, p);
        }

        // Bottom info strip
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0x00, 0x08, 0x16, 230);
        canvas.DrawRoundRect(new SKRoundRect(new SKRect(8, 96, 345, 118), 4, 4), p);
        p.Style = SKPaintStyle.Stroke;
        p.StrokeWidth = 1;
        p.Color = new SKColor(0, 230, 255, 25);
        canvas.DrawRoundRect(new SKRoundRect(new SKRect(8, 96, 345, 118), 4, 4), p);

        DrawMonoText(canvas, $"FW #{FwBuild}", 24, 111, 8, new SKColor(255, 255, 255, 114), SKTextAlign.Left);
        canvas.DrawLine(72, 100, 72, 114, new SKPaint { Color = new SKColor(255, 255, 255, 25), StrokeWidth = 1 });
        DrawMonoText(canvas, $"HEAP {HeapBytes / 1024}KB", 82, 111, 8, new SKColor(255, 255, 255, 114), SKTextAlign.Left);
        canvas.DrawLine(152, 100, 152, 114, new SKPaint { Color = new SKColor(255, 255, 255, 25), StrokeWidth = 1 });
        int uh = (int)(UptimeSec / 3600); int um = (int)(UptimeSec % 3600 / 60);
        DrawMonoText(canvas, $"UP {uh}h {um:00}m", 162, 111, 8, new SKColor(255, 255, 255, 114), SKTextAlign.Left);
        canvas.DrawLine(250, 100, 250, 114, new SKPaint { Color = new SKColor(255, 255, 255, 25), StrokeWidth = 1 });
        DrawMonoText(canvas, "ventilatie/state", 260, 111, 8, new SKColor(0, 230, 255, 140), SKTextAlign.Left);
    }

    private void DrawNetNode(SKCanvas canvas, SKPaint p, float t,
        float cx, float cy, string label, string status, SKColor accentColor, SKColor statusColor,
        float pulsePeriod, float pulsePhase, string? subLabel = null)
    {
        // Outer node circle
        p.Style = SKPaintStyle.Fill;
        p.Color = new SKColor(0x00, 0x0f, 0x2d, 230);
        canvas.DrawCircle(cx, cy, 26, p);
        p.Style = SKPaintStyle.Stroke;
        p.StrokeWidth = 1.5f;
        p.Color = accentColor.WithAlpha(89);
        canvas.DrawCircle(cx, cy, 26, p);

        // Pulse ring
        float pr = 0.5f + 0.5f * MathF.Sin((t + pulsePhase) * (2 * MathF.PI / pulsePeriod));
        p.Style = SKPaintStyle.Stroke;
        p.StrokeWidth = 1;
        p.Color = accentColor.WithAlpha((byte)(51 * (0.3f + 0.7f * pr)));
        canvas.DrawCircle(cx, cy, 26, p);

        // Labels
        using var lPaint = new SKPaint { Typeface = _tfRajdhani, TextSize = 9, TextAlign = SKTextAlign.Center, Color = accentColor, IsAntialias = true, FakeBoldText = true };
        canvas.DrawText(label, cx, cy - 7, lPaint);
        using var sPaint = new SKPaint { Typeface = _tfMono, TextSize = 7.5f, TextAlign = SKTextAlign.Center, Color = new SKColor(255, 255, 255, 127), IsAntialias = true };
        canvas.DrawText(status, cx, cy + 4, sPaint);
        if (subLabel != null)
        {
            using var subP = new SKPaint { Typeface = _tfMono, TextSize = 7, TextAlign = SKTextAlign.Center, Color = new SKColor(255, 255, 255, 89), IsAntialias = true };
            canvas.DrawText(subLabel, cx, cy + 14, subP);
        }

        // Status LED
        float pulseA = 0.5f + 0.5f * MathF.Sin(t * MathF.PI * 2 / pulsePeriod + pulsePhase);
        p.Style = SKPaintStyle.Fill;
        p.Color = statusColor.WithAlpha((byte)(150 + 100 * pulseA));
        canvas.DrawCircle(cx, cy + 20, 3.5f, p);
    }

    // ── Shared helpers ────────────────────────────────────────────────────────

    private void DrawLedStrip(SKCanvas canvas, float x, float y, float w, float h, float alpha)
    {
        using var ledPaint = new SKPaint
        {
            IsAntialias = true,
            Style = SKPaintStyle.Fill,
            Shader = SKShader.CreateLinearGradient(
                new SKPoint(x, y), new SKPoint(x + w, y),
                new[]
                {
                    new SKColor(0, 230, 255, 0),
                    new SKColor(0, 230, 255, (byte)(229 * alpha)),
                    new SKColor(0, 210, 255, (byte)(255 * alpha)),
                    new SKColor(0, 230, 255, (byte)(229 * alpha)),
                    new SKColor(0, 230, 255, 0)
                },
                new float[] { 0f, 0.25f, 0.5f, 0.75f, 1f },
                SKShaderTileMode.Clamp)
        };
        canvas.DrawRoundRect(new SKRoundRect(new SKRect(x, y, x + w, y + h), 1.2f, 1.2f), ledPaint);
    }

    private void DrawZoneIndicator(SKCanvas canvas, float cx, float cy, string label, bool relay, float t, float pulse, float period)
    {
        using var p = new SKPaint { IsAntialias = true };
        p.Style = SKPaintStyle.Fill;
        if (relay)
            p.Color = new SKColor(0, 232, 122, (byte)(100 + 100 * pulse));
        else
            p.Color = new SKColor(255, 255, 255, 46);
        canvas.DrawCircle(cx, cy, 4.5f, p);

        using var txtPaint = new SKPaint
        {
            Typeface = _tfRajdhani, TextSize = 10, TextAlign = SKTextAlign.Left,
            Color = relay ? new SKColor(0, 232, 122, 204) : new SKColor(255, 255, 255, 76),
            IsAntialias = true, FakeBoldText = true
        };
        canvas.DrawText($"{label} {(relay ? "ON" : "OFF")}", cx + 9, cy + 4, txtPaint);
    }

    private void DrawZoneIndicatorRight(SKCanvas canvas, float cx, float cy, string label, bool relay, float t, float pulse)
    {
        using var p = new SKPaint { IsAntialias = true };
        p.Style = SKPaintStyle.Fill;
        if (relay)
            p.Color = new SKColor(0, 232, 122, (byte)(100 + 100 * pulse));
        else
            p.Color = new SKColor(255, 255, 255, 46);
        canvas.DrawCircle(cx, cy, 4.5f, p);

        using var txtPaint = new SKPaint
        {
            Typeface = _tfRajdhani, TextSize = 10, TextAlign = SKTextAlign.Left,
            Color = relay ? new SKColor(0, 232, 122, 204) : new SKColor(255, 255, 255, 76),
            IsAntialias = true, FakeBoldText = true
        };
        canvas.DrawText($"{label} {(relay ? "ON" : "OFF")}", cx + 9, cy + 4, txtPaint);
    }

    private void DrawMonoText(SKCanvas canvas, string text, float x, float y, float size, SKColor color, SKTextAlign align)
    {
        using var paint = new SKPaint { Typeface = _tfMono, TextSize = size, TextAlign = align, Color = color, IsAntialias = true };
        canvas.DrawText(text, x, y, paint);
    }

    // ── Color helpers ─────────────────────────────────────────────────────────

    private static SKColor GetTempColor(float temp) => temp switch
    {
        < 15  => new SKColor(0x00, 0x99, 0xff),
        < 25  => new SKColor(0x00, 0xcc, 0xff),
        < 33  => new SKColor(0x00, 0xe8, 0x7a),
        < 40  => new SKColor(0xff, 0xcc, 0x00),
        < 50  => new SKColor(0xff, 0x7a, 0x45),
        _     => new SKColor(0xff, 0x44, 0x22)
    };

    private static string GetTempLabel(float temp) => temp switch
    {
        < 15  => "COLD",
        < 25  => "COOL",
        < 33  => "IDEAL",
        < 40  => "WARM",
        < 50  => "HOT",
        _     => "CRITICAL"
    };
}
