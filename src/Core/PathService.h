// Application storage paths.
//
// This is the only caller of the Platform path provider. Every other component
// asks this service, so path resolution happens once and cannot drift between
// call sites.
#pragma once

#include <string>
#include <string_view>

#include "Platform/Result.h"
#include "Platform/StoragePaths.h"

namespace tmpp::core
{
    /**
     * @brief Provides the application's storage locations.
     */
    class PathService
    {
    public:
        /**
         * @brief Resolves the paths for an application.
         *
         * @param applicationName Directory created under %LOCALAPPDATA%.
         */
        [[nodiscard]] static Result<PathService> Create(std::string_view applicationName,
                                                        std::string_view settingsFileName);

        [[nodiscard]] std::string const& SettingsDirectory() const noexcept { return m_paths.settingsDirectory; }
        [[nodiscard]] std::string const& SettingsFile() const noexcept { return m_paths.settingsFile; }
        [[nodiscard]] std::string const& ApplicationDirectory() const noexcept { return m_paths.applicationDirectory; }

        /**
         * @brief Builds a path inside the settings directory.
         */
        [[nodiscard]] std::string SettingsPath(std::string_view fileName) const;

    private:
        PathService() = default;

        platform::StoragePaths m_paths;
    };
}
