// Persisted user settings.
//
// The application is unpackaged, so WinRT's ApplicationData is unavailable and
// the settings file lives under %LOCALAPPDATA%. Three properties matter more than
// the schema itself:
//
//   1. A damaged or hand-edited file must not prevent startup. It is preserved
//      for inspection and defaults are used instead.
//   2. Missing keys must not be an error. A file written by an older version is
//      expected to lack newer keys.
//   3. Out-of-range values must be clamped on load, never at the point of use.
//      That way the sampler can trust what it is given.
#pragma once

#include <cstdint>
#include <string>

#include "Domain/SamplingConfig.h"
#include "Platform/Result.h"

namespace tmpp::core
{
    /// Theme preference.
    enum class ThemeMode
    {
        System = 0,
        Light,
        Dark,
    };

    /// User interface language.
    enum class LanguageMode
    {
        System = 0,
        English,
        SimplifiedChinese,
    };

    /// How the selected chart history window maps to a duration.
    enum class HistoryWindow
    {
        Seconds60 = 0,
        Minutes5,
        Minutes30,
    };

    /**
     * @brief Which page the application opens on.
     *
     * Stored as an enum rather than a page name string so an unknown value in a hand-edited file
     * falls back to the default instead of selecting nothing.
     */
    enum class StartupPage
    {
        Processes = 0,
        Performance,
        Details,
        LastUsed,
    };

    /**
     * @brief How the performance page orders its disk rows.
     */
    enum class DiskSortOrder
    {
        /// By the device index the firmware assigns, which is the order Windows' own tool uses and the
        /// order the devices are physically attached in.
        DeviceIndex = 0,

        /// By the first volume letter the device backs, so a user looking for C: finds it by scanning
        /// alphabetically rather than by knowing which physical device it lives on.
        FirstDriveLetter,
    };

    /**
     * @brief A chart's appearance, as the user configured it.
     *
     * The colour is stored as its three channels rather than packed into one integer: a settings
     * file is meant to be read and edited by hand, and "r: 76, g: 194, b: 255" is far easier to
     * adjust than 0xFF4CC2FF.
     */
    struct ChartStyle
    {
        uint8_t red{0x4C};
        uint8_t green{0xC2};
        uint8_t blue{0xFF};

        /// Stroke width in effective pixels.
        ///
        /// Defaults to a hairline. The chart is a reading of a value over time and the line only has
        /// to mark where that value is; a thicker stroke draws attention to the ink rather than to the
        /// shape, and it hides the samples it is joining.
        double lineWidth{1.0};

        /// Clamped so a hand-edited file cannot produce an invisible or a solid chart.
        [[nodiscard]] double ClampedLineWidth() const noexcept
        {
            if (lineWidth < 0.5)
            {
                return 0.5;
            }
            if (lineWidth > 8.0)
            {
                return 8.0;
            }
            return lineWidth;
        }
    };

    /**
     * @brief Everything the user can change that must survive a restart.
     */
    struct Settings
    {
        /// Schema version, so a future change can migrate rather than guess.
        static constexpr int CURRENT_VERSION = 1;

        int version{CURRENT_VERSION};

        // General
        ThemeMode theme{ThemeMode::System};
        LanguageMode language{LanguageMode::System};
        bool alwaysOnTop{false};
        bool minimizeToTray{false};
        bool closeToTray{false};
        bool runAsAdmin{false};

        // Sampling
        uint32_t intervalMs{domain::sampling::DEFAULT_INTERVAL_MS};
        bool reduceWhenMinimized{true};
        uint32_t minimizedIntervalMs{domain::sampling::MINIMIZED_INTERVAL_MS};
        HistoryWindow historyWindow{HistoryWindow::Seconds60};

        // Startup
        /// Which page to open on. LastUsed resumes wherever the user left off.
        StartupPage startupPage{StartupPage::Processes};

        /// How the disk rows are ordered. A machine's disks are named by the volumes they back, so the
        /// two orders give genuinely different lists rather than the same one reversed.
        DiskSortOrder diskSortOrder{DiskSortOrder::DeviceIndex};

        /// The page in use when the application last closed. Only consulted when startupPage is
        /// LastUsed, and updated on every close so the two cannot disagree.
        StartupPage lastUsedPage{StartupPage::Processes};

        // Appearance
        /// Per-metric chart styles, so each series can be coloured separately as the original
        /// allows. Keyed by the same Section the performance page uses.
        ChartStyle cpuChart{0x4C, 0xC2, 0xFF, 1.0};
        // The memory series is violet, matching the original's memory chart.
        ChartStyle memoryChart{155, 140, 255, 1.0};
        ChartStyle diskChart{0x6E, 0xD8, 0xB0, 1.0};
        ChartStyle networkChart{0xFF, 0xC1, 0x57, 1.0};
        ChartStyle gpuChart{0xFF, 0x8A, 0xA8, 1.0};

        // Layout
        /// Width of the application's navigation rail. Negative means "not yet decided", so the
        /// platform default applies.
        double navigationWidth{-1.0};

        /// Whether the navigation rail is expanded.
        ///
        /// Defaults to collapsed: the rail is a reminder of where the pages are, and the icons alone
        /// identify them. A first run should show the content rather than the menu. The state is
        /// persisted, so a user who expands it keeps it expanded.
        bool navigationExpanded{false};

        /// Width of the performance page's own sidebar. Negative means "not yet decided".
        double performanceSidebarWidth{-1.0};

        // Window placement
        int32_t windowX{-1}; ///< Negative means "not yet decided".
        int32_t windowY{-1};
        int32_t windowWidth{1100};
        int32_t windowHeight{700};
        bool windowMaximized{false};

        /**
         * @brief The chart style for a metric, by section index.
         *
         * @param sectionIndex Index into the performance page's section list.
         * @return The style, or the CPU style for an index that has none yet, so a caller always
         *         has something to draw with rather than a blank series.
         */
        [[nodiscard]] ChartStyle const& ChartStyleFor(int sectionIndex) const noexcept
        {
            switch (sectionIndex)
            {
                case 0:
                    return cpuChart;
                case 1:
                    return memoryChart;
                case 2:
                    return diskChart;
                case 3:
                    return networkChart;
                case 4:
                    return gpuChart;
                default:
                    return cpuChart;
            }
        }

        /// Mutable accessor, for the settings page.
        [[nodiscard]] ChartStyle& MutableChartStyleFor(int sectionIndex) noexcept
        {
            switch (sectionIndex)
            {
                case 1:
                    return memoryChart;
                case 2:
                    return diskChart;
                case 3:
                    return networkChart;
                case 4:
                    return gpuChart;
                case 0:
                default:
                    return cpuChart;
            }
        }

        /// Effective interval after applying the minimised-window policy.
        [[nodiscard]] uint32_t EffectiveIntervalMs(bool minimized) const noexcept
        {
            if (minimized && reduceWhenMinimized)
            {
                return domain::sampling::ClampInterval(minimizedIntervalMs);
            }
            return domain::sampling::ClampInterval(intervalMs);
        }

        /// History window as a duration in seconds.
        [[nodiscard]] uint32_t HistorySeconds() const noexcept
        {
            switch (historyWindow)
            {
                case HistoryWindow::Minutes5:
                    return 300;
                case HistoryWindow::Minutes30:
                    return 1800;
                case HistoryWindow::Seconds60:
                default:
                    return 60;
            }
        }
    };

    /**
     * @brief Loads and stores the settings file.
     *
     * Not thread-safe; the owner serialises access.
     */
    class SettingsStore
    {
    public:
        /**
         * @param filePath Full path of the settings file. Its directory is created
         *        on save if missing.
         */
        explicit SettingsStore(std::string path) : m_path(std::move(path)) {}

        /**
         * @brief Loads settings from disk.
         *
         * Never fails in a way that should stop startup:
         *   * missing file        -> defaults
         *   * unparsable file     -> defaults, and the file is renamed to .bak
         *   * missing keys        -> defaults for those keys
         *   * out-of-range values -> clamped
         *
         * @return The loaded settings. Check DamagedFilePreserved() afterwards to
         *         decide whether to tell the user.
         */
        [[nodiscard]] Settings Load();

        /**
         * @brief Writes settings to disk atomically.
         */
        [[nodiscard]] VoidResult Save(Settings const& settings) const;

        /// True when the last Load found a damaged file and preserved it.
        [[nodiscard]] bool DamagedFilePreserved() const noexcept { return m_damagedFilePreserved; }

        [[nodiscard]] std::string const& FilePath() const noexcept { return m_path; }

    private:
        std::string m_path;
        bool m_damagedFilePreserved{false};
    };
}
