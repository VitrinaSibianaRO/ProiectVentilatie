using Microsoft.Maui.Controls;

namespace ProiectVentilatie.Mobile.Controls;

public partial class CollapsibleSection : ContentView
{
    public static readonly BindableProperty TitleProperty =
        BindableProperty.Create(nameof(Title), typeof(string), typeof(CollapsibleSection), string.Empty);

    public static readonly BindableProperty IsExpandedProperty =
        BindableProperty.Create(nameof(IsExpanded), typeof(bool), typeof(CollapsibleSection), false,
            propertyChanged: OnIsExpandedChanged);

    // Use InnerContent to avoid conflict with ContentView.Content property
    public static readonly BindableProperty InnerContentProperty =
        BindableProperty.Create(nameof(InnerContent), typeof(View), typeof(CollapsibleSection), null);

    public string Title
    {
        get => (string)GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    public bool IsExpanded
    {
        get => (bool)GetValue(IsExpandedProperty);
        set => SetValue(IsExpandedProperty, value);
    }

    public View InnerContent
    {
        get => (View)GetValue(InnerContentProperty);
        set => SetValue(InnerContentProperty, value);
    }

    public string ChevronText => IsExpanded ? "▲" : "▼";

    public Command ToggleCommand { get; }

    public CollapsibleSection()
    {
        InitializeComponent();
        ToggleCommand = new Command(() => IsExpanded = !IsExpanded);
    }

    private static void OnIsExpandedChanged(BindableObject bindable, object oldValue, object newValue)
    {
        ((CollapsibleSection)bindable).OnPropertyChanged(nameof(ChevronText));
    }
}
