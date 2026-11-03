#include "UI/WinRTUI.h"

#include "UI/Views/SettingsPage.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>

using winrt::Microsoft::UI::Xaml::Controls::Border;
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

            // LastUsed keeps its persisted value (3) after removing the old Details option (2).
            m_startupPage.MinWidth(180.0);
            m_startupPage.Items().Append(winrt::box_value(ui::Localize(L"Processes")));
            m_startupPage.Items().Append(winrt::box_value(ui::Localize(L"Performance")));
            m_startupPage.Items().Append(winrt::box_value(ui::Localize(L"Last used page")));
            m_startupPage.SelectedIndex(m_settings.startupPage == core::StartupPage::LastUsed ? 2 : m_settings.startupPage == core::StartupPage::Performance ? 1 : 0);
            m_startupPage.SelectionChanged(
                [this](winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
                    int32_t const index = m_startupPage.SelectedIndex();
                    if (index >= 0)
                    {
                        m_settings.startupPage = index == 2 ? core::StartupPage::LastUsed : index == 1 ? core::StartupPage::Performance : core::StartupPage::Processes;
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
            m_diskOrder.Items().Append(winrt::box_value(ui::Localize(L"By device number")));
            m_diskOrder.Items().Append(winrt::box_value(ui::Localize(L"By first drive letter")));
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

            _addRow(section, L"\xE774", L"Language", L"Choose the display language", m_language);
            m_language.MinWidth(180.0);
            for (auto const* label : {L"System default", L"English", L"Simplified Chinese"})
                m_language.Items().Append(winrt::box_value(ui::Localize(label)));
            m_language.SelectedIndex(static_cast<int32_t>(m_settings.language));
            m_language.SelectionChanged([this](auto const&, auto const&) {
                if (ui::LocalizationInProgress()) return;
                auto const index = m_language.SelectedIndex();
                if (index >= 0) {
                    m_settings.language = static_cast<core::LanguageMode>(index);
                    _notifyChange();
                }
            });

            _addRow(section, L"\xE790", L"Mica backdrop", L"Use the Windows Mica material for the window background", m_micaBackdrop);
            m_micaBackdrop.MinWidth(0.0);
            m_micaBackdrop.Width(40.0);
            m_micaBackdrop.OnContent(winrt::box_value(L""));
            m_micaBackdrop.OffContent(winrt::box_value(L""));
            m_micaBackdrop.IsOn(m_settings.micaBackdrop);
            m_micaBackdrop.Toggled([this](auto const&, auto const&) {
                m_settings.micaBackdrop = m_micaBackdrop.IsOn();
                _notifyChange();
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
            m_theme.Items().Append(winrt::box_value(ui::Localize(L"Use system setting")));
            m_theme.Items().Append(winrt::box_value(ui::Localize(L"Light")));
            m_theme.Items().Append(winrt::box_value(ui::Localize(L"Dark")));
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
            m_alwaysOnTop.IsOn(m_settings.alwaysOnTop);
            m_alwaysOnTop.MinWidth(0.0);
            m_alwaysOnTop.Width(40.0);
            m_alwaysOnTop.OnContent(winrt::box_value(ui::Localize(L"")));
            m_alwaysOnTop.OffContent(winrt::box_value(ui::Localize(L"")));
            m_alwaysOnTop.Toggled([this](auto const&, auto const&) {
                m_settings.alwaysOnTop = m_alwaysOnTop.IsOn();
                _notifyChange();
            });
        }

        {
            StackPanel section = _addSection(body, L"Sampling");
            _addRow(section, L"\xE823", L"Sampling interval (ms)",
                L"Time between samples, from 500 to 5000 milliseconds", m_samplingInterval);
            m_samplingInterval.Width(180.0);
            m_samplingInterval.Minimum(domain::sampling::MIN_INTERVAL_MS);
            m_samplingInterval.Maximum(domain::sampling::MAX_INTERVAL_MS);
            m_samplingInterval.SmallChange(100.0);
            m_samplingInterval.LargeChange(500.0);
            m_samplingInterval.SpinButtonPlacementMode(winrt::Microsoft::UI::Xaml::Controls::NumberBoxSpinButtonPlacementMode::Inline);
            m_samplingInterval.Value(m_settings.intervalMs);
            winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(m_samplingInterval, L"SamplingInterval");
            m_samplingInterval.ValueChanged([this](auto const&, auto const& args) {
                double const value = args.NewValue();
                if (!std::isfinite(value)) return;
                auto const interval = static_cast<uint32_t>(std::round(std::clamp(value,
                    static_cast<double>(domain::sampling::MIN_INTERVAL_MS), static_cast<double>(domain::sampling::MAX_INTERVAL_MS))));
                if (value != interval) { m_samplingInterval.Value(interval); return; }
                if (interval == m_settings.intervalMs) return;
                m_settings.intervalMs = interval;
                _notifyChange();
            });
            _addRow(section, L"\xE81C", L"History duration (s)",
                L"Keep 60 to 1800 seconds of performance chart history", m_historySeconds);
            m_historySeconds.Width(180.0);
            m_historySeconds.Minimum(domain::sampling::MIN_HISTORY_SECONDS);
            m_historySeconds.Maximum(domain::sampling::MAX_HISTORY_SECONDS);
            m_historySeconds.SmallChange(30.0);
            m_historySeconds.LargeChange(60.0);
            m_historySeconds.SpinButtonPlacementMode(winrt::Microsoft::UI::Xaml::Controls::NumberBoxSpinButtonPlacementMode::Inline);
            m_historySeconds.Value(m_settings.HistorySeconds());
            winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(m_historySeconds, L"HistoryDuration");
            m_historySeconds.ValueChanged([this](auto const&, auto const& args) {
                double const value = args.NewValue();
                if (!std::isfinite(value)) return;
                auto const seconds = static_cast<uint32_t>(std::round(std::clamp(value,
                    static_cast<double>(domain::sampling::MIN_HISTORY_SECONDS), static_cast<double>(domain::sampling::MAX_HISTORY_SECONDS))));
                if (value != seconds) { m_historySeconds.Value(seconds); return; }
                if (seconds == m_settings.HistorySeconds()) return;
                m_settings.historySeconds = seconds;
                _notifyChange();
            });
        }

        {
            StackPanel section = _addSection(body, L"Charts");
            _addRow(section,
                L"\xE790",
                L"Opaque chart fill",
                L"Use solid colour below the line instead of a translucent fill",
                m_opaqueFill);
            m_opaqueFill.IsOn(m_settings.opaqueChartFill);
            m_opaqueFill.MinWidth(0.0);
            m_opaqueFill.Width(40.0);
            m_opaqueFill.OnContent(winrt::box_value(ui::Localize(L"")));
            m_opaqueFill.OffContent(winrt::box_value(ui::Localize(L"")));
            m_opaqueFill.Toggled([this](auto const&, auto const&) {
                m_settings.opaqueChartFill = m_opaqueFill.IsOn();
                m_fillOpacityRow.Visibility(m_settings.opaqueChartFill
                        ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                        : winrt::Microsoft::UI::Xaml::Visibility::Visible);
                _notifyChange();
            });
            m_fillOpacityRow = Grid();
            m_fillOpacityRow.Padding(ThicknessHelper::FromLengths(16.0, 10.0, 16.0, 10.0));
            m_fillOpacityRow.ColumnDefinitions().Append(controls::MakeStarColumn());
            m_fillOpacityRow.ColumnDefinitions().Append(controls::MakeAutoColumn());
            auto label = controls::MakeText(L"Fill opacity", 14.0);
            label.VerticalAlignment(VerticalAlignment::Center);
            m_fillOpacityRow.Children().Append(label);
            auto opacityControls = controls::MakeRow(10.0);
            opacityControls.HorizontalAlignment(HorizontalAlignment::Right);
            m_fillOpacity = Slider();
            m_fillOpacity.Minimum(0.0);
            m_fillOpacity.Maximum(100.0);
            m_fillOpacity.StepFrequency(1.0);
            m_fillOpacity.Width(180.0);
            m_fillOpacity.Value(m_settings.chartFillOpacity * 100.0);
            opacityControls.Children().Append(m_fillOpacity);
            m_fillOpacityText = controls::MakeText(
                std::to_wstring(static_cast<int>(m_settings.chartFillOpacity * 100.0)) + L"%", 12.0, true);
            m_fillOpacityText.Width(44.0);
            m_fillOpacityText.TextAlignment(winrt::Microsoft::UI::Xaml::TextAlignment::Right);
            m_fillOpacityText.VerticalAlignment(VerticalAlignment::Center);
            opacityControls.Children().Append(m_fillOpacityText);
            Grid::SetColumn(opacityControls, 1);
            m_fillOpacityRow.Children().Append(opacityControls);
            m_fillOpacityRow.Visibility(m_settings.opaqueChartFill ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                                                                   : winrt::Microsoft::UI::Xaml::Visibility::Visible);
            section.Children().Append(m_fillOpacityRow);
            m_fillOpacity.ValueChanged([this](auto const&, auto const&) {
                m_settings.chartFillOpacity = m_fillOpacity.Value() / 100.0;
                m_fillOpacityText.Text(ui::Localize(std::to_wstring(static_cast<int>(m_fillOpacity.Value())) + L"%"));
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
            if (metric.sectionIndex == 0)
            {
                _addRow(
                    section, L"\xE9D9", L"Show kernel times", L"Overlay kernel CPU time on the CPU charts", m_kernelTimes);
                m_kernelTimes.MinWidth(0.0);
                m_kernelTimes.Width(40.0);
                m_kernelTimes.OnContent(winrt::box_value(L""));
                m_kernelTimes.OffContent(winrt::box_value(L""));
                m_kernelTimes.IsOn(m_settings.showCpuKernelTimes);
                m_kernelTimes.Toggled([this](auto const&, auto const&) {
                    m_settings.showCpuKernelTimes = m_kernelTimes.IsOn();
                    _notifyChange();
                });
            }
            if (metric.sectionIndex == 2)
            {
                _addRow(section, L"\xEDA2", L"Show cumulative reads and writes",
                    L"Show session read and write totals in the disk sidebar", m_diskCumulativeBytes);
                m_diskCumulativeBytes.MinWidth(0.0);
                m_diskCumulativeBytes.Width(40.0);
                m_diskCumulativeBytes.OnContent(winrt::box_value(L""));
                m_diskCumulativeBytes.OffContent(winrt::box_value(L""));
                m_diskCumulativeBytes.IsOn(m_settings.showDiskCumulativeBytes);
                winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(
                    m_diskCumulativeBytes, L"ShowDiskCumulativeBytes");
                m_diskCumulativeBytes.Toggled([this](auto const&, auto const&) {
                    m_settings.showDiskCumulativeBytes = m_diskCumulativeBytes.IsOn();
                    _notifyChange();
                });

                _addRow(section, L"\xE9D2", L"Disk sidebar metric",
                    L"Choose the chart and live reading shown for each disk", m_diskSidebarMetric);
                m_diskSidebarMetric.MinWidth(180.0);
                m_diskSidebarMetric.Items().Append(winrt::box_value(ui::Localize(L"Busy percentage")));
                m_diskSidebarMetric.Items().Append(winrt::box_value(ui::Localize(L"Bandwidth")));
                m_diskSidebarMetric.SelectedIndex(static_cast<int32_t>(m_settings.diskSidebarMetric));
                winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(
                    m_diskSidebarMetric, L"DiskSidebarMetric");
                m_diskSidebarMetric.SelectionChanged([this](auto const&, auto const&) {
                    if (ui::LocalizationInProgress()) return;
                    auto const index = m_diskSidebarMetric.SelectedIndex();
                    if (index >= 0 && index <= 1)
                    {
                        m_settings.diskSidebarMetric = static_cast<core::DiskSidebarMetric>(index);
                        _notifyChange();
                    }
                });
            }
            _addChartStyleGroup(section, metric.title, metric.sectionIndex);
            if (metric.sectionIndex == 0)
                _addChartStyleGroup(section, L"Kernel time", 5);
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

        m_chartRows.emplace_back();
        ChartRow& row = m_chartRows.back();

        // --- Colour row: the framework's colour picker ---
        {
            Grid colourRow = Grid();
            colourRow.Padding(ThicknessHelper::FromLengths(16.0, 10.0, 16.0, 10.0));
            colourRow.ColumnDefinitions().Append(controls::MakeStarColumn());
            colourRow.ColumnDefinitions().Append(controls::MakeAutoColumn());

            StackPanel label = controls::MakeStack(1.0);
            label.VerticalAlignment(VerticalAlignment::Center);
            label.Children().Append(controls::MakeText(sectionIndex == 5 ? L"Kernel line colour" : L"Line colour", 14.0));
            label.Children().Append(controls::MakeText(sectionIndex == 5 ? L"Pick the colour used to plot kernel CPU time" : L"Pick the colour used to plot this metric", 12.0, true));
            Grid::SetColumn(label, 0);
            colourRow.Children().Append(label);

            // Keep the full spectrum and RGB controls in a flyout opened from the colour preview.
            row.picker = winrt::Microsoft::UI::Xaml::Controls::ColorPicker();
            row.picker.IsAlphaEnabled(false);
            row.picker.IsAlphaSliderVisible(false);
            row.picker.IsAlphaTextInputVisible(false);
            row.picker.IsHexInputVisible(true);
            row.picker.IsColorSpectrumVisible(true);
            row.picker.ColorSpectrumShape(winrt::Microsoft::UI::Xaml::Controls::ColorSpectrumShape::Box);
            row.picker.IsMoreButtonVisible(false);
            row.picker.IsColorChannelTextInputVisible(true);
            row.picker.Width(300.0);
            row.picker.VerticalAlignment(VerticalAlignment::Center);
            row.picker.Color(_makeColor(style));
            winrt::Microsoft::UI::Xaml::Controls::Flyout pickerFlyout;
            pickerFlyout.Content(row.picker);
            winrt::Microsoft::UI::Xaml::Controls::Button colourButton;
            colourButton.Flyout(pickerFlyout);
            colourButton.Padding(ThicknessHelper::FromLengths(10.0, 6.0, 10.0, 6.0));
            winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
                colourButton, ui::Localize(std::wstring{title} + L" line colour"));
            StackPanel preview = controls::MakeRow(10.0);
            row.swatch = Border();
            row.swatch.Width(24.0);
            row.swatch.Height(18.0);
            row.swatch.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(4.0));
            preview.Children().Append(row.swatch);
            row.hex = controls::MakeText(L"", 13.0);
            row.hex.VerticalAlignment(VerticalAlignment::Center);
            preview.Children().Append(row.hex);
            winrt::Microsoft::UI::Xaml::Controls::FontIcon chevron;
            chevron.Glyph(L"\xE70D");
            chevron.FontSize(10.0);
            preview.Children().Append(chevron);
            colourButton.Content(preview);
            Grid::SetColumn(colourButton, 1);
            colourRow.Children().Append(colourButton);

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
            label.Children().Append(controls::MakeText(sectionIndex == 5 ? L"Kernel line width" : L"Line width", 14.0));
            label.Children().Append(controls::MakeText(L"Thickness of the plotted line", 12.0, true));
            Grid::SetColumn(label, 0);
            widthRow.Children().Append(label);

            StackPanel widthControls = controls::MakeRow(10.0);
            widthControls.VerticalAlignment(VerticalAlignment::Center);
            widthControls.HorizontalAlignment(HorizontalAlignment::Right);

            row.width = Slider();
            row.width.Minimum(0.5);
            row.width.Maximum(8.0);
            row.width.StepFrequency(0.5);
            row.width.Value(style.ClampedLineWidth());
            row.width.Width(180.0);
            row.width.VerticalAlignment(VerticalAlignment::Center);
            widthControls.Children().Append(row.width);

            row.readout = controls::MakeText(L"", 12.0, true);
            row.readout.VerticalAlignment(VerticalAlignment::Center);
            row.readout.Width(60.0);
            row.readout.TextAlignment(winrt::Microsoft::UI::Xaml::TextAlignment::Right);
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
            row.swatch.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(_makeColor(style)));
            wchar_t hex[8]{};
            swprintf_s(hex, L"#%02X%02X%02X", style.red, style.green, style.blue);
            row.hex.Text(ui::Localize(hex));

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
            row.readout.Text(ui::Localize(text));

            row.suppress = false;
        }
    }

    void SettingsPage::_notifyChange()
    {
        if (ui::LocalizationInProgress()) return;
        _refreshFromSettings();
        if (m_onChange)
        {
            m_onChange(m_settings);
        }
    }
}
