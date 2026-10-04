// The Settings page.
//
// Reached from the bottom of the navigation rail, which is where the original puts it. It exposes
// the three appearance choices that were asked for -- each chart's colour and line width, and
// whether the window stays on top -- plus the startup page.
//
// The page edits a copy of the settings and reports it back through a callback rather than writing
// the file itself. The application owns the settings file and decides when to save, so a slider
// dragged across its range does not produce a write per pixel.
#pragma once

#include "UI/WinRTUI.h"

#include <functional>
#include <memory>
#include <vector>

#include "Core/Settings.h"
#include "UI/Theming/Controls.h"
#include "UI/Theming/Theme.h"

namespace tmpp::ui
{
    /**
     * @brief Builds and owns the settings page.
     */
    class SettingsPage
    {
    public:
        /**
         * @param settings The current settings, used to seed every control.
         * @param onChange Called whenever a value changes, with the updated settings.
         */
        SettingsPage(core::Settings const& settings, std::function<void(core::Settings const&)> onChange);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// The settings as edited so far.
        [[nodiscard]] core::Settings const& Current() const noexcept { return m_settings; }

    private:
        /// One metric's appearance: the framework colour picker and the line width slider.
        struct ChartRow
        {
            /// The framework's own colour picker. Used rather than three channel sliders because it
            /// already provides a spectrum, a live preview and hex entry, and it is the control a user
            /// of this platform expects for choosing a colour.
            winrt::Microsoft::UI::Xaml::Controls::ColorPicker picker{nullptr};

            winrt::Microsoft::UI::Xaml::Controls::Slider width{nullptr};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock readout{nullptr};

            /// Set while the picker is being written to from code. Without it the resulting
            /// ColorChanged event would feed back into the settings, and refreshing the page while a
            /// change is being handled re-enters the handler.
            bool suppress{false};
        };

        /**
         * @brief Builds one settings section: a header, then its rows inside a card.
         *
         * The original groups its settings this way, and the grouping is what makes a long list
         * navigable: a flat list of a dozen controls gives the eye nothing to anchor on.
         *
         * @param title Section heading.
         * @return The stack to add rows to.
         */
        winrt::Microsoft::UI::Xaml::Controls::StackPanel _addSection(
            winrt::Microsoft::UI::Xaml::Controls::StackPanel const& body,
            wchar_t const* title);

        /**
         * @brief Builds one settings row: icon and description on the left, control on the right.
         *
         * @param section Section to add the row to.
         * @param glyph Segoe Fluent Icons glyph.
         * @param title Row title.
         * @param description One line explaining what the row does.
         * @param outControl Receives the control the row was built around, so the caller can wire it.
         */
        template <typename TControl>
        void _addRow(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& section,
                     wchar_t const* glyph,
                     wchar_t const* title,
                     wchar_t const* description,
                     TControl& outControl)
        {
            winrt::Microsoft::UI::Xaml::Controls::Grid row;
            row.Padding(winrt::Microsoft::UI::Xaml::ThicknessHelper::FromLengths(16.0, 12.0, 16.0, 12.0));

            // The label column takes the slack so the control sits against the right edge at any
            // width, which is the arrangement the original uses.
            row.ColumnDefinitions().Append(controls::MakeAutoColumn());
            row.ColumnDefinitions().Append(controls::MakeStarColumn());
            row.ColumnDefinitions().Append(controls::MakeAutoColumn());

            winrt::Microsoft::UI::Xaml::Controls::FontIcon icon;
            icon.Glyph(glyph);
            icon.FontSize(16.0);
            icon.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            icon.Margin(winrt::Microsoft::UI::Xaml::ThicknessHelper::FromLengths(0.0, 0.0, 16.0, 0.0));
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(icon, 0);
            row.Children().Append(icon);

            winrt::Microsoft::UI::Xaml::Controls::StackPanel label = controls::MakeStack(1.0);
            label.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            label.Children().Append(controls::MakeText(title, 14.0));
            label.Children().Append(controls::MakeText(description, 12.0, true));
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(label, 1);
            row.Children().Append(label);

            outControl = TControl();
            outControl.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(outControl, 2);
            row.Children().Append(outControl);

            section.Children().Append(row);
        }

        void _buildLayout();

        /**
         * @brief Adds one metric's appearance controls and retains them for updates.
         *
         * @param section Section to add the group to.
         * @param title Name of the metric.
         * @param sectionIndex Index used to select the style from the settings.
         */
        void _addChartStyleGroup(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& section,
                                 wchar_t const* title,
                                 int sectionIndex);

        /// Pushes the edited values into the controls, so a change made elsewhere is reflected.
        void _refreshFromSettings();

        /// Applies the current settings to the live chart styles and persists them.
        void _notifyChange();

        /// The colour a row currently represents.
        [[nodiscard]] winrt::Windows::UI::Color _rowColor(ChartRow const& row) const;

        core::Settings m_settings;
        std::function<void(core::Settings const&)> m_onChange;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};

        /// One entry per metric, in the order they are added.
        std::vector<ChartRow> m_chartRows;

        /// Section indices parallel to m_chartRows, so a row's index can be mapped to its style.
        std::vector<int> m_rowSections;

        winrt::Microsoft::UI::Xaml::Controls::CheckBox m_alwaysOnTop{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::ComboBox m_startupPage{nullptr};

        /// Follow the system, or force light or dark.
        winrt::Microsoft::UI::Xaml::Controls::ComboBox m_theme{nullptr};

        /// How the performance page orders its disk rows.
        winrt::Microsoft::UI::Xaml::Controls::ComboBox m_diskOrder{nullptr};
    };
}
