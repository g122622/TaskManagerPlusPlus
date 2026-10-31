#include "Platform/StoragePaths.h"

#include "Platform/Windows/WindowsString.h"

#include <windows.h>
#include <shlobj.h>

#include <vector>
#include <winver.h>
#pragma comment(lib, "version.lib")

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
    std::string ApplicationVersion()
    {
        std::vector<wchar_t> path(32768);
        auto const length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) return {};
        DWORD ignored = 0;
        DWORD const bytes = GetFileVersionInfoSizeW(path.data(), &ignored);
        if (bytes == 0) return {};
        std::vector<unsigned char> buffer(bytes);
        if (!GetFileVersionInfoW(path.data(), 0, bytes, buffer.data())) return {};
        VS_FIXEDFILEINFO* version = nullptr;
        UINT size = 0;
        if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&version), &size) ||
            size < sizeof(VS_FIXEDFILEINFO) || version->dwSignature != 0xFEEF04BD) return {};
        std::string result = std::to_string(HIWORD(version->dwFileVersionMS)) + "." +
            std::to_string(LOWORD(version->dwFileVersionMS)) + "." + std::to_string(HIWORD(version->dwFileVersionLS));
        if (LOWORD(version->dwFileVersionLS) != 0) result += "." + std::to_string(LOWORD(version->dwFileVersionLS));
        return result;
    }

}
