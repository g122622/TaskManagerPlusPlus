// Storage locations the application needs, expressed without a platform header.
#pragma once

#include <string>

#include "Platform/Result.h"

namespace tmpp::platform
{
    /**
     * @brief The directories this application reads and writes.
     *
     * Resolved once at startup. The application is unpackaged, so the WinRT
     * ApplicationData API is unavailable and these paths must be derived from the
     * known-folder API instead.
     */
    struct StoragePaths
    {
        /// Settings and other per-user state, e.g.
        /// %LOCALAPPDATA%\TaskManagerPlusPlus. Local rather than roaming: the
        /// contents describe this machine.
        std::string settingsDirectory;

        /// Full path of the settings file.
        std::string settingsFile;

        /// Directory holding the executable, used to resolve adjacent assets.
        std::string applicationDirectory;
    };

    /**
     * @brief Resolves the application's storage locations.
     *
     * @param applicationName Directory name to create under %LOCALAPPDATA%.
     * @param settingsFileName Name of the settings file, without a directory.
     */
    [[nodiscard]] Result<StoragePaths> ResolveStoragePaths(std::string_view applicationName,
                                                            std::string_view settingsFileName);
}
