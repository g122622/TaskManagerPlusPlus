#include "Core/Settings.h"

#include "Platform/FileSystem.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include <algorithm>

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

        json const& startup = _subObject(root, "startup");
        settings.startupPage = _getEnum(startup, "page", settings.startupPage, 3);
        settings.lastUsedPage = _getEnum(startup, "lastUsedPage", settings.lastUsedPage, 3);

        // The disk order lives with the startup settings because it is a display preference rather than
        // a property of any one page.
        settings.diskSortOrder = _getEnum(startup, "diskSortOrder", settings.diskSortOrder, 1);

        // Appearance. Each chart's colour is stored as three channels rather than one packed
        // integer, so the file stays readable and hand-editable.
        json const& appearance = _subObject(root, "appearance");
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
        readStyle("memory", settings.memoryChart);
        readStyle("disk", settings.diskChart);
        readStyle("network", settings.networkChart);
        readStyle("gpu", settings.gpuChart);

        json const& layout = _subObject(root, "layout");
        settings.navigationWidth = _getNumber<double>(layout, "navigationWidth", settings.navigationWidth);
        settings.performanceSidebarWidth =
            _getNumber<double>(layout, "performanceSidebarWidth", settings.performanceSidebarWidth);
        settings.navigationExpanded = _getBool(layout, "navigationExpanded", settings.navigationExpanded);

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
            settings.windowWidth = 1100;
        }
        if (settings.windowHeight < 200)
        {
            settings.windowHeight = 700;
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
        appearance["memory"] = writeStyle(settings.memoryChart);
        appearance["disk"] = writeStyle(settings.diskChart);
        appearance["network"] = writeStyle(settings.networkChart);
        appearance["gpu"] = writeStyle(settings.gpuChart);
        root["appearance"] = std::move(appearance);

        json layout;
        layout["navigationWidth"] = settings.navigationWidth;
        layout["performanceSidebarWidth"] = settings.performanceSidebarWidth;
        layout["navigationExpanded"] = settings.navigationExpanded;
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
