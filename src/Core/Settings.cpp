#include "Core/Settings.h"

#include "Platform/FileSystem.h"

#include <algorithm>

#include <cmath>
#include <nlohmann/json.hpp>

namespace tmpp::core
{
    namespace
    {
        using json = nlohmann::json;

        /**
         * @brief Reads an integer, returning the fallback when absent or wrong type.
         *
         * A user who hand-edits the file and writes "theme": "purple" should get the
         * default rather than a crash or an exception escaping to the caller.
         */
        template <typename T>
        [[nodiscard]] T _getNumber(json const& object, char const* key, T fallback)
        {
            auto const found = object.find(key);
            if (found == object.end() || !found->is_number())
            {
                return fallback;
            }
            return found->get<T>();
        }

        /**
         * @brief Reads a boolean, returning the fallback when absent or wrong type.
         */
        [[nodiscard]] bool _getBool(json const& object, char const* key, bool fallback)
        {
            auto const found = object.find(key);
            if (found == object.end() || !found->is_boolean())
            {
                return fallback;
            }
            return found->get<bool>();
        }

        /**
         * @brief Reads an enum stored as an integer, clamped to the valid range.
         */
        template <typename Enum>
        [[nodiscard]] Enum _getEnum(json const& object, char const* key, Enum fallback, int maxValue)
        {
            int const raw = _getNumber<int>(object, key, static_cast<int>(fallback));
            if (raw < 0 || raw > maxValue)
            {
                return fallback;
            }
            return static_cast<Enum>(raw);
        }

        /**
         * @brief Narrows an object to a sub-object, or returns an empty object.
         */
        [[nodiscard]] json const& _subObject(json const& root, char const* key)
        {
            static json const empty = json::object();
            auto const found = root.find(key);
            if (found == root.end() || !found->is_object())
            {
                return empty;
            }
            return *found;
        }
    }

    Settings SettingsStore::Load()
    {
        Settings settings;
        m_damagedFilePreserved = false;

        if (!platform::FileExists(m_path))
        {
            // First run. Not an error, and not worth logging as one.
            return settings;
        }

        auto const contents = platform::ReadFileToString(m_path);
        if (!contents.Success())
        {
            return settings;
        }

        // Parsed without exceptions: the file may have been edited by hand, and a
        // parse error must degrade to defaults rather than terminate startup.
        json root = json::parse(contents.Value(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            // Preserve the damaged file instead of overwriting it on the next save,
            // so the user can see what went wrong.
            std::string const backup = m_path + ".bak";
            if (platform::RenameFile(m_path, backup).Success())
            {
                m_damagedFilePreserved = true;
            }
            return settings;
        }

        settings.version = _getNumber<int>(root, "version", Settings::CURRENT_VERSION);

        json const& general = _subObject(root, "general");
        settings.theme = _getEnum(general, "theme", settings.theme, 2);
        settings.language = _getEnum(general, "language", settings.language, 2);
        settings.alwaysOnTop = _getBool(general, "alwaysOnTop", settings.alwaysOnTop);
        settings.minimizeToTray = _getBool(general, "minimizeToTray", settings.minimizeToTray);
        settings.closeToTray = _getBool(general, "closeToTray", settings.closeToTray);
        settings.runAsAdmin = _getBool(general, "runAsAdmin", settings.runAsAdmin);

        json const& sampling = _subObject(root, "sampling");
        // Clamp on load: the sampler must never receive a value outside the
        // permitted range, whatever the file says.
        settings.intervalMs = domain::sampling::ClampInterval(
            _getNumber<uint32_t>(sampling, "intervalMs", settings.intervalMs));
        settings.reduceWhenMinimized = _getBool(sampling, "reduceWhenMinimized", settings.reduceWhenMinimized);
        settings.minimizedIntervalMs = domain::sampling::ClampInterval(
            _getNumber<uint32_t>(sampling, "minimizedIntervalMs", settings.minimizedIntervalMs));
        settings.historyWindow = _getEnum(sampling, "historyWindow", settings.historyWindow, 2);
        if (sampling.contains("historySeconds"))
        {
            double const seconds = _getNumber<double>(sampling, "historySeconds", settings.HistorySeconds());
            if (std::isfinite(seconds)) settings.historySeconds = static_cast<uint32_t>(std::clamp(seconds,
                static_cast<double>(domain::sampling::MIN_HISTORY_SECONDS),
                static_cast<double>(domain::sampling::MAX_HISTORY_SECONDS)));
        }

        json const& startup = _subObject(root, "startup");
        settings.startupPage = _getEnum(startup, "page", settings.startupPage, 3);
        settings.lastUsedPage = _getEnum(startup, "lastUsedPage", settings.lastUsedPage, 3);
        if (settings.startupPage == StartupPage::Details) settings.startupPage = StartupPage::Processes;
        if (settings.lastUsedPage != StartupPage::Processes && settings.lastUsedPage != StartupPage::Performance)
            settings.lastUsedPage = StartupPage::Processes;

        // The disk order lives with the startup settings because it is a display preference rather than
        // a property of any one page.
        settings.diskSortOrder = _getEnum(startup, "diskSortOrder", settings.diskSortOrder, 1);

        // Appearance. Each chart's colour is stored as three channels rather than one packed
        // integer, so the file stays readable and hand-editable.
        json const& appearance = _subObject(root, "appearance");
        settings.opaqueChartFill = _getBool(appearance, "opaqueChartFill", settings.opaqueChartFill);
        settings.micaBackdrop = _getBool(appearance, "micaBackdrop", settings.micaBackdrop);
        settings.showCpuKernelTimes = _getBool(appearance, "showCpuKernelTimes", settings.showCpuKernelTimes);
        settings.showDiskCumulativeBytes = _getBool(appearance, "showDiskCumulativeBytes", settings.showDiskCumulativeBytes);
        settings.diskSidebarMetric = _getEnum(appearance, "diskSidebarMetric", settings.diskSidebarMetric, 1);
        double const fillOpacity = _getNumber<double>(appearance, "chartFillOpacity", settings.chartFillOpacity);
        if (std::isfinite(fillOpacity)) settings.chartFillOpacity = std::clamp(fillOpacity, 0.0, 1.0);
        auto readStyle = [&appearance](char const* key, ChartStyle& style) {
            json const& node = _subObject(appearance, key);
            style.red = static_cast<uint8_t>(std::clamp(_getNumber<int>(node, "r", style.red), 0, 255));
            style.green = static_cast<uint8_t>(std::clamp(_getNumber<int>(node, "g", style.green), 0, 255));
            style.blue = static_cast<uint8_t>(std::clamp(_getNumber<int>(node, "b", style.blue), 0, 255));
            style.lineWidth = _getNumber<double>(node, "lineWidth", style.lineWidth);
            // The getter clamps on use, but clamping here too means a save after a load cannot
            // write back a value the user never intended.
            style.lineWidth = style.ClampedLineWidth();
        };

        readStyle("cpu", settings.cpuChart);
        readStyle("cpuKernel", settings.cpuKernelChart);
        readStyle("memory", settings.memoryChart);
        readStyle("disk", settings.diskChart);
        readStyle("network", settings.networkChart);
        readStyle("gpu", settings.gpuChart);

        json const& layout = _subObject(root, "layout");
        settings.navigationWidth = _getNumber<double>(layout, "navigationWidth", settings.navigationWidth);
        settings.performanceSidebarWidth =
            _getNumber<double>(layout, "performanceSidebarWidth", settings.performanceSidebarWidth);
        settings.processSidebarWidth =
            _getNumber<double>(layout, "processSidebarWidth", settings.processSidebarWidth);
        settings.processSidebarVisible =
            _getBool(layout, "processSidebarVisible", settings.processSidebarVisible);
        settings.navigationExpanded = _getBool(layout, "navigationExpanded", settings.navigationExpanded);

        // The process list's column widths. A non-array entry, or a value that is not a positive finite
        // number, is skipped rather than carried through: the view validates each width against its own
        // column's bounds and falls back to the default, so the only job here is to reject nonsense.
        if (auto const found = layout.find("processColumnWidths");
            found != layout.end() && found->is_array())
        {
            settings.processColumnWidths.clear();
            for (auto const& entry : *found)
            {
                if (!entry.is_number())
                {
                    settings.processColumnWidths.push_back(0.0);
                    continue;
                }

                double const width = entry.get<double>();
                settings.processColumnWidths.push_back(width > 0.0 && width < 4000.0 ? width : 0.0);
            }
        }

        settings.processColumnOrder.clear();
        if (auto const order = layout.find("processColumnOrder"); order != layout.end() && order->is_array())
            for (auto const& value : *order)
                if (value.is_number_integer() && value.get<int64_t>() >= 0 && value.get<int64_t>() < 1024)
                    settings.processColumnOrder.push_back(value.get<uint32_t>());
        if (auto const found = layout.find("processColumnVisible"); found != layout.end() && found->is_array()) {
            settings.processColumnVisible.clear();
            for (auto const& entry : *found)
                settings.processColumnVisible.push_back(entry.is_boolean() ? entry.get<bool>() : true);
        }
        json const& window = _subObject(root, "window");
        settings.windowX = _getNumber<int32_t>(window, "x", settings.windowX);
        settings.windowY = _getNumber<int32_t>(window, "y", settings.windowY);
        settings.windowWidth = _getNumber<int32_t>(window, "width", settings.windowWidth);
        settings.windowHeight = _getNumber<int32_t>(window, "height", settings.windowHeight);
        settings.windowMaximized = _getBool(window, "maximized", settings.windowMaximized);

        // A zero or negative size would make the window impossible to grab, so a
        // corrupt value is treated as absent.
        if (settings.windowWidth < 200)
        {
            settings.windowWidth = Settings{}.windowWidth;
        }
        if (settings.windowHeight < 200)
        {
            settings.windowHeight = Settings{}.windowHeight;
        }

        return settings;
    }

    VoidResult SettingsStore::Save(Settings const& settings) const
    {
        json root;
        root["version"] = settings.version;

        json general;
        general["theme"] = static_cast<int>(settings.theme);
        general["language"] = static_cast<int>(settings.language);
        general["alwaysOnTop"] = settings.alwaysOnTop;
        general["minimizeToTray"] = settings.minimizeToTray;
        general["closeToTray"] = settings.closeToTray;
        general["runAsAdmin"] = settings.runAsAdmin;
        root["general"] = std::move(general);

        json sampling;
        sampling["intervalMs"] = settings.intervalMs;
        sampling["reduceWhenMinimized"] = settings.reduceWhenMinimized;
        sampling["minimizedIntervalMs"] = settings.minimizedIntervalMs;
        sampling["historyWindow"] = static_cast<int>(settings.historyWindow);
        sampling["historySeconds"] = settings.HistorySeconds();
        root["sampling"] = std::move(sampling);

        json startup;
        startup["page"] = static_cast<int>(settings.startupPage);
        startup["lastUsedPage"] = static_cast<int>(settings.lastUsedPage);
        startup["diskSortOrder"] = static_cast<int>(settings.diskSortOrder);
        root["startup"] = std::move(startup);

        json appearance;
        auto writeStyle = [](ChartStyle const& style) {
            json node;
            node["r"] = static_cast<int>(style.red);
            node["g"] = static_cast<int>(style.green);
            node["b"] = static_cast<int>(style.blue);
            node["lineWidth"] = style.lineWidth;
            return node;
        };

        appearance["cpu"] = writeStyle(settings.cpuChart);
        appearance["cpuKernel"] = writeStyle(settings.cpuKernelChart);
        appearance["micaBackdrop"] = settings.micaBackdrop;
        appearance["memory"] = writeStyle(settings.memoryChart);
        appearance["disk"] = writeStyle(settings.diskChart);
        appearance["network"] = writeStyle(settings.networkChart);
        appearance["opaqueChartFill"] = settings.opaqueChartFill;
        appearance["showCpuKernelTimes"] = settings.showCpuKernelTimes;
        appearance["showDiskCumulativeBytes"] = settings.showDiskCumulativeBytes;
        appearance["diskSidebarMetric"] = static_cast<int>(settings.diskSidebarMetric);
        appearance["chartFillOpacity"] = settings.chartFillOpacity;
        appearance["gpu"] = writeStyle(settings.gpuChart);
        root["appearance"] = std::move(appearance);

        json layout;
        layout["navigationWidth"] = settings.navigationWidth;
        layout["performanceSidebarWidth"] = settings.performanceSidebarWidth;
        layout["processSidebarWidth"] = settings.processSidebarWidth;
        layout["processSidebarVisible"] = settings.processSidebarVisible;
        layout["navigationExpanded"] = settings.navigationExpanded;

        // Written as an array, and only when the user has dragged something. An empty array is a
        // legitimate value that means "never dragged", so it is written too rather than omitted: the file
        // then says what the state is rather than leaving it to be inferred from a missing key.
        layout["processColumnWidths"] = settings.processColumnWidths;
        layout["processColumnVisible"] = settings.processColumnVisible;
        layout["processColumnOrder"] = settings.processColumnOrder;
        root["layout"] = std::move(layout);

        json window;
        window["x"] = settings.windowX;
        window["y"] = settings.windowY;
        window["width"] = settings.windowWidth;
        window["height"] = settings.windowHeight;
        window["maximized"] = settings.windowMaximized;
        root["window"] = std::move(window);

        // Indented because this file is meant to be readable and editable by hand.
        std::string const serialized = root.dump(2);
        return platform::AtomicWriteFile(m_path, serialized);
    }
}
