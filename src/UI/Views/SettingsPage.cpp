#include "UI/WinRTUI.h"

#include "UI/Views/SettingsPage.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include <algorithm>
#include <string>

using winrt::Microsoft::UI::Xaml::Controls::Border;
using winrt::Microsoft::UI::Xaml::Controls::CheckBox;
using winrt::Microsoft::UI::Xaml::Controls::ComboBox;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::Slider;
using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
using winrt::Microsoft::UI::Xaml::HorizontalAlignment;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;
using winrt::Microsoft::UI::Xaml::VerticalAlignment;

namespace tmpp::ui
{
    namespace
    {
        /// Font size of the heading.
        constexpr double HEADING_FONT_SIZE = 22.0;

        /// Size of the colour swatch that previews a metric's line.
        constexpr double SWATCH_WIDTH = 42.0;
        constexpr double SWATCH_HEIGHT = 10.0;

        /// Width of the channel sliders. Narrow enough that three fit side by side with labels.
        constexpr double CHANNEL_SLIDER_WIDTH = 130.0;

        /// The metrics whose appearance is configurable, paired with their section index.
        struct MetricSpec
        {
            wchar_t const* title;
            int sectionIndex;
        };

        constexpr MetricSpec METRICS[]{
            {L"CPU", 0},
            {L"Memory", 1},
            {L"Disk", 2},
            {L"Network", 3},
            {L"GPU", 4},
        };

        [[nodiscard]] winrt::Windows::UI::Color _makeColor(core::ChartStyle const& style)
        {
            return winrt::Windows::UI::Color{0xFF, style.red, style.green, style.blue};
        }
    }

    SettingsPage::SettingsPage(core::Settings const& settings, std::function<void(core::Settings const&)> onChange)
        : m_settings(settings), m_onChange(std::move(onChange))
    {
        _buildLayout();
    }

    void SettingsPage::_buildLayout()
    {
        m_root = Grid();
        m_root.Padding(ThicknessHelper::FromLengths(metrics::CONTENT_LEFT_INSET, 8.0, metrics::PAGE_MARGIN, 8.0));

        // Heading, then a scrolling body: the appearance groups are taller than a short window, so
        // the body scrolls while the heading stays put. A star row is required for the scroller to
        // have a bounded height, which is what lets it scroll at all.
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeStarRow());

        TextBlock heading = controls::MakeHeading(L"Settings", HEADING_FONT_SIZE);
        heading.Margin(ThicknessHelper::FromLengths(0.0, 8.0, 0.0, 12.0));
        Grid::SetRow(heading, 0);
        m_root.Children().Append(heading);

        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scroller;
        scroller.VerticalScrollBarVisibility(winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Auto);
        scroller.HorizontalScrollBarVisibility(winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Disabled);

        StackPanel body = controls::MakeStack(24.0);

        // --- General -----------------------------------------------------------
        {
            StackPanel section = _addSection(body, L"General");

            _addRow(section,
                    L"\xE8A7",
                    L"Open on",
                    L"Which page is shown when the application starts",
                    m_startupPage);

            // The order matches the StartupPage enum, so the selected index is the enum value and no
            // lookup table is needed to map between them.
            m_startupPage.MinWidth(180.0);
            m_startupPage.Items().Append(winrt::box_value(winrt::hstring{L"Processes"}));
            m_startupPage.Items().Append(winrt::box_value(winrt::hstring{L"Performance"}));
            m_startupPage.Items().Append(winrt::box_value(winrt::hstring{L"Details"}));
            m_startupPage.Items().Append(winrt::box_value(winrt::hstring{L"Last used page"}));
            m_startupPage.SelectedIndex(static_cast<int32_t>(m_settings.startupPage));
            m_startupPage.SelectionChanged(
                [this](winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
                    int32_t const index = m_startupPage.SelectedIndex();
                    if (index >= 0)
                    {
                        m_settings.startupPage = static_cast<core::StartupPage>(index);
                        _notifyChange();
                    }
                });

            // Disk order. The two orders give genuinely different lists rather than the same one
            // reversed, because a device is named by the volumes it backs: the device the firmware calls
            // 2 may be the one backing C:.
            _addRow(section,
                    L"\xEDA2",
                    L"Disk order",
                    L"Which drive is listed first on the performance page",
                    m_diskOrder);

            // The order matches the DiskSortOrder enum, so the selected index is the enum value.
            m_diskOrder.MinWidth(180.0);
            m_diskOrder.Items().Append(winrt::box_value(winrt::hstring{L"By device number"}));
            m_diskOrder.Items().Append(winrt::box_value(winrt::hstring{L"By first drive letter"}));
            m_diskOrder.SelectedIndex(static_cast<int32_t>(m_settings.diskSortOrder));
            m_diskOrder.SelectionChanged(
                [this](winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
                    int32_t const index = m_diskOrder.SelectedIndex();
                    if (index >= 0)
                    {
                        m_settings.diskSortOrder = static_cast<core::DiskSortOrder>(index);
                        _notifyChange();
                    }
                });

            // Theme. The setting existed in the file but was never applied, so choosing one did
            // nothing at all.
            _addRow(section,
                    L"\xE790",
                    L"App theme",
                    L"Follow the system, or force light or dark",
                    m_theme);

            // The order matches the ThemeMode enum, so the selected index is the enum value and no
            // lookup table is needed to map between them.
            m_theme.MinWidth(180.0);
            m_theme.Items().Append(winrt::box_value(winrt::hstring{L"Use system setting"}));
            m_theme.Items().Append(winrt::box_value(winrt::hstring{L"Light"}));
            m_theme.Items().Append(winrt::box_value(winrt::hstring{L"Dark"}));
            m_theme.SelectedIndex(static_cast<int32_t>(m_settings.theme));
            m_theme.SelectionChanged(
                [this](winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
                    int32_t const index = m_theme.SelectedIndex();
                    if (index >= 0)
                    {
                        m_settings.theme = static_cast<core::ThemeMode>(index);
                        _notifyChange();
                    }
                });

            _addRow(section,
                    L"\xE840",
                    L"Keep the window on top",
                    L"Leave the window above other windows",
                    m_alwaysOnTop);
            m_alwaysOnTop.IsChecked(m_settings.alwaysOnTop);
            m_alwaysOnTop.Checked([this](winrt::Windows::Foundation::IInspectable const&,
                                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
                m_settings.alwaysOnTop = m_alwaysOnTop.IsChecked().GetBoolean();
                _notifyChange();
            });
            m_alwaysOnTop.Unchecked([this](winrt::Windows::Foundation::IInspectable const&,
                                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
                m_settings.alwaysOnTop = m_alwaysOnTop.IsChecked().GetBoolean();
                _notifyChange();
            });
        }

        // --- Chart appearance --------------------------------------------------
        //
        // One section per metric rather than one long list: each metric owns a colour and a width, and
        // grouping them makes clear which control belongs to which chart.
        for (MetricSpec const& metric : METRICS)
        {
            StackPanel section = _addSection(body, metric.title);
            _addChartStyleGroup(section, metric.title, metric.sectionIndex);
        }

        scroller.Content(body);
        Grid::SetRow(scroller, 1);
        m_root.Children().Append(scroller);

        _refreshFromSettings();
    }

    winrt::Microsoft::UI::Xaml::Controls::StackPanel SettingsPage::_addSection(
        winrt::Microsoft::UI::Xaml::Controls::StackPanel const& body, wchar_t const* title)
    {
        // The heading sits outside the card, as the original does: it names the group rather than
        // being part of it.
        StackPanel section = controls::MakeStack(6.0);
        section.Children().Append(controls::MakeHeading(title, 14.0));

        Border card = controls::MakeCard();
        card.Padding(ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 0.0));

        StackPanel rows = controls::MakeStack(0.0);
        card.Child(rows);
        section.Children().Append(card);

        body.Children().Append(section);
        return rows;
    }

    void SettingsPage::_addChartStyleGroup(StackPanel const& section, wchar_t const* title, int sectionIndex)
    {
        (void)title;
        core::ChartStyle& style = m_settings.MutableChartStyleFor(sectionIndex);

        ChartRow row;

        // --- Colour row: the framework's colour picker ---
        {
            Grid colourRow = Grid();
            colourRow.Padding(ThicknessHelper::FromLengths(16.0, 10.0, 16.0, 10.0));
            colourRow.ColumnDefinitions().Append(controls::MakeStarColumn());
            colourRow.ColumnDefinitions().Append(controls::MakeAutoColumn());

            StackPanel label = controls::MakeStack(1.0);
            label.VerticalAlignment(VerticalAlignment::Center);
            label.Children().Append(controls::MakeText(L"Line colour", 14.0));
            label.Children().Append(controls::MakeText(L"Pick the colour used to plot this metric", 12.0, true));
            Grid::SetColumn(label, 0);
            colourRow.Children().Append(label);

            // Expanded so the spectrum, the preview and the RGB fields are all usable without an
            // extra click; collapsed, the control is a single swatch button and the user would have to
            // open it before seeing any of that.
            row.picker = winrt::Microsoft::UI::Xaml::Controls::ColorPicker();
            row.picker.IsAlphaEnabled(false);
            row.picker.IsAlphaSliderVisible(false);
            row.picker.IsAlphaTextInputVisible(false);
            row.picker.IsHexInputVisible(true);
            row.picker.IsColorSpectrumVisible(true);
            row.picker.ColorSpectrumShape(winrt::Microsoft::UI::Xaml::Controls::ColorSpectrumShape::Box);
            row.picker.IsMoreButtonVisible(true);
            row.picker.MinWidth(320.0);
            row.picker.VerticalAlignment(VerticalAlignment::Center);
            row.picker.Color(_makeColor(style));
            Grid::SetColumn(row.picker, 1);
            colourRow.Children().Append(row.picker);

            row.picker.ColorChanged(
                [this, sectionIndex, &row](winrt::Microsoft::UI::Xaml::Controls::ColorPicker const& sender,
                                           winrt::Microsoft::UI::Xaml::Controls::ColorChangedEventArgs const&) {
                    // Ignored while the page is writing into the picker. Without this the refresh that
                    // follows a change would raise ColorChanged again and re-enter this handler.
                    if (row.suppress)
                    {
                        return;
                    }

                    winrt::Windows::UI::Color const color = sender.Color();
                    core::ChartStyle& target = m_settings.MutableChartStyleFor(sectionIndex);
                    target.red = color.R;
                    target.green = color.G;
                    target.blue = color.B;
                    _notifyChange();
                });

            section.Children().Append(colourRow);
        }

        // --- Line width row ---
        {
            Grid widthRow = Grid();
            widthRow.Padding(ThicknessHelper::FromLengths(16.0, 10.0, 16.0, 10.0));
            widthRow.ColumnDefinitions().Append(controls::MakeStarColumn());
            widthRow.ColumnDefinitions().Append(controls::MakeAutoColumn());

            StackPanel label = controls::MakeStack(1.0);
            label.VerticalAlignment(VerticalAlignment::Center);
            label.Children().Append(controls::MakeText(L"Line width", 14.0));
            label.Children().Append(controls::MakeText(L"Thickness of the plotted line", 12.0, true));
            Grid::SetColumn(label, 0);
            widthRow.Children().Append(label);

            StackPanel widthControls = controls::MakeRow(10.0);
            widthControls.VerticalAlignment(VerticalAlignment::Center);

            row.width = Slider();
            row.width.Minimum(0.5);
            row.width.Maximum(8.0);
            row.width.StepFrequency(0.5);
            row.width.Value(style.ClampedLineWidth());
            row.width.Width(220.0);
            row.width.VerticalAlignment(VerticalAlignment::Center);
            widthControls.Children().Append(row.width);

            row.readout = controls::MakeText(L"", 12.0, true);
            row.readout.VerticalAlignment(VerticalAlignment::Center);
            row.readout.MinWidth(60.0);
            widthControls.Children().Append(row.readout);

            Grid::SetColumn(widthControls, 1);
            widthRow.Children().Append(widthControls);

            row.width.ValueChanged(
                [this, sectionIndex, &row](
                    winrt::Windows::Foundation::IInspectable const& sender,
                    winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&) {
                    if (row.suppress)
                    {
                        return;
                    }

                    auto const slider = sender.try_as<Slider>();
                    if (slider == nullptr)
                    {
                        return;
                    }
                    m_settings.MutableChartStyleFor(sectionIndex).lineWidth = slider.Value();
                    _notifyChange();
                });

            section.Children().Append(widthRow);
        }

        m_chartRows.push_back(row);
        m_rowSections.push_back(sectionIndex);
    }

    void SettingsPage::_refreshFromSettings()
    {
        for (size_t i = 0; i < m_chartRows.size(); ++i)
        {
            ChartRow& row = m_chartRows[i];
            core::ChartStyle const& style = m_settings.ChartStyleFor(m_rowSections[i]);

            // Writing into a control raises its change event, which would call back into this page and
            // restart the handler. The flag makes the round trip harmless.
            row.suppress = true;

            winrt::Windows::UI::Color const color = _makeColor(style);
            if (row.picker != nullptr && row.picker.Color() != color)
            {
                row.picker.Color(color);
            }

            if (row.width != nullptr && std::abs(row.width.Value() - style.ClampedLineWidth()) > 0.01)
            {
                row.width.Value(style.ClampedLineWidth());
            }

            // The readout makes an exact width reachable, which a slider on its own cannot guarantee.
            std::string text = std::to_string(style.ClampedLineWidth()).substr(0, 3);
            text += " px";
            row.readout.Text(winrt::to_hstring(text));

            row.suppress = false;
        }
    }

    void SettingsPage::_notifyChange()
    {
        _refreshFromSettings();
        if (m_onChange)
        {
            m_onChange(m_settings);
        }
    }
}
