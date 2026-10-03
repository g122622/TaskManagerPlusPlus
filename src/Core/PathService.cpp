#include "Core/PathService.h"

namespace tmpp::core
{
    Result<PathService> PathService::Create(std::string_view applicationName, std::string_view settingsFileName)
    {
        auto const paths = platform::ResolveStoragePaths(applicationName, settingsFileName);
        if (!paths.Success())
        {
            return paths.GetError();
        }

        PathService service;
        service.m_paths = paths.Value();
        return service;
    }

    std::string PathService::SettingsPath(std::string_view fileName) const
    {
        if (m_paths.settingsDirectory.empty())
        {
            return std::string(fileName);
        }

        std::string path = m_paths.settingsDirectory;
        if (path.back() != '\\' && path.back() != '/')
        {
            path.push_back('\\');
        }
        path.append(fileName);
        return path;
    }
}
