#include "Platform/StoragePaths.h"

#include "Platform/Windows/WindowsString.h"

#include <windows.h>
#include <shlobj.h>

#include <vector>

namespace tmpp::platform
{
    namespace
    {
        /**
         * @brief Reads the executable's own directory, including a trailing separator.
         */
        [[nodiscard]] Result<std::string> _applicationDirectory()
        {
            // GetModuleFileNameW reports the required length when the buffer is too
            // small and sets ERROR_INSUFFICIENT_BUFFER; grow rather than truncate, so
            // a long install path does not silently break asset lookup.
            std::vector<wchar_t> buffer(MAX_PATH);
            for (;;)
            {
                DWORD const length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    return Error{ErrorCode::NativeFailure,
                                 "GetModuleFileNameW failed with error " + std::to_string(GetLastError()),
                                 "ResolveStoragePaths"};
                }

                if (length < buffer.size())
                {
                    std::wstring path{buffer.data(), length};
                    auto const separator = path.find_last_of(L"\\/");
                    if (separator != std::wstring::npos)
                    {
                        path.resize(separator + 1);
                    }
                    return ToUtf8(path);
                }

                buffer.resize(buffer.size() * 2);
            }
        }
    }

    Result<StoragePaths> ResolveStoragePaths(std::string_view applicationName, std::string_view settingsFileName)
    {
        PWSTR localAppDataRaw = nullptr;
        HRESULT const hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppDataRaw);
        if (FAILED(hr) || localAppDataRaw == nullptr)
        {
            if (localAppDataRaw != nullptr)
            {
                CoTaskMemFree(localAppDataRaw);
            }
            return Error{ErrorCode::NativeFailure,
                         "SHGetKnownFolderPath(FOLDERID_LocalAppData) failed",
                         "ResolveStoragePaths"};
        }

        std::wstring localAppData{localAppDataRaw};
        CoTaskMemFree(localAppDataRaw);

        StoragePaths paths;

        std::wstring settingsDirectory = localAppData;
        if (settingsDirectory.back() != L'\\')
        {
            settingsDirectory.push_back(L'\\');
        }
        settingsDirectory.append(ToUtf16(applicationName));

        paths.settingsDirectory = ToUtf8(settingsDirectory);
        paths.settingsFile = paths.settingsDirectory + "\\" + std::string(settingsFileName);

        auto const applicationDirectory = _applicationDirectory();
        if (!applicationDirectory.Success())
        {
            return applicationDirectory.GetError();
        }
        paths.applicationDirectory = applicationDirectory.Value();

        return paths;
    }
}
