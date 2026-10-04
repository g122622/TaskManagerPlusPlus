#include "Platform/Windows/ProcessIconReader.h"

// Include order matters here: commoncontrols.h declares IImageList but uses types that commctrl.h
// defines, so it has to be included after it.
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <string>
#include <vector>

namespace tmpp::platform
{
    namespace
    {
        /// Resolves a process's executable path. Empty when it cannot be read, which is the normal
        /// outcome for a process that has exited or one this user cannot open.
        [[nodiscard]] std::wstring _imagePathOf(uint32_t pid)
        {
            // QUERY_LIMITED_INFORMATION rather than QUERY_INFORMATION: it is enough to read the image
            // name and is granted for far more processes, including ones running as another user.
            HANDLE const process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process == nullptr)
            {
                return {};
            }

            std::wstring path(MAX_PATH, L'\0');
            DWORD length = static_cast<DWORD>(path.size());

            // The first call reports the required length when its buffer is too small, so the buffer is
            // grown and the call repeated rather than assuming MAX_PATH is enough. A long path in a deep
            // directory is common.
            if (::QueryFullProcessImageNameW(process, 0, path.data(), &length) == FALSE)
            {
                path.resize(length + 1);
                length = static_cast<DWORD>(path.size());
                if (::QueryFullProcessImageNameW(process, 0, path.data(), &length) == FALSE)
                {
                    ::CloseHandle(process);
                    return {};
                }
            }

            ::CloseHandle(process);
            path.resize(length);
            return path;
        }

        [[nodiscard]] std::string _narrow(std::wstring const& wide)
        {
            if (wide.empty())
            {
                return {};
            }

            int const needed = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                                     nullptr, 0, nullptr, nullptr);
            if (needed <= 0)
            {
                return {};
            }

            std::string narrow(static_cast<size_t>(needed), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), narrow.data(),
                                  needed, nullptr, nullptr);
            return narrow;
        }
    }

    bool ProcessIconReader::HasNoImage(std::string const& imageName) noexcept
    {
        // These are the kernel's own pseudo-processes and the idle process. None of them is backed by an
        // executable file, so the shell lookup fails for each of them every time it is tried; recognising
        // them here keeps that failure off the render path and out of the cache.
        return imageName.empty() || imageName == "System" || imageName == "Registry" ||
               imageName == "Memory Compression" || imageName == "Idle" || imageName == "Secure System";
    }

    Result<IconPixels> ProcessIconReader::Read(uint32_t pid, int32_t size) const
    {
        std::wstring const path = _imagePathOf(pid);
        if (path.empty())
        {
            return Error{ErrorCode::NativeFailure,
                         "the process's executable path could not be read",
                         "ProcessIconReader::Read"};
        }

        // SHGFI_SYSICONINDEX with SHGFI_SMALLICON asks the shell for the index of the small icon it
        // already has for this file. Going through the system image list rather than SHGFI_ICON means the
        // shell's own cache does the work, and the icon comes back at the size the list is configured for.
        SHFILEINFOW fileInfo{};
        if (::SHGetFileInfoW(path.c_str(), 0, &fileInfo, sizeof(fileInfo),
                             SHGFI_SYSICONINDEX | SHGFI_SMALLICON) == 0)
        {
            return Error{ErrorCode::NativeFailure,
                         "the shell has no icon for " + _narrow(path),
                         "ProcessIconReader::Read"};
        }

        IImageList* imageList = nullptr;
        if (FAILED(::SHGetImageList(SHIL_SMALL, IID_IImageList, reinterpret_cast<void**>(&imageList))) ||
            imageList == nullptr)
        {
            return Error{ErrorCode::NativeFailure,
                         "the shell's small image list is unavailable",
                         "ProcessIconReader::Read"};
        }

        HICON icon = nullptr;
        HRESULT const extracted = imageList->GetIcon(fileInfo.iIcon, ILD_TRANSPARENT, &icon);
        imageList->Release();

        if (FAILED(extracted) || icon == nullptr)
        {
            return Error{ErrorCode::NativeFailure,
                         "the shell's icon could not be extracted from its image list",
                         "ProcessIconReader::Read"};
        }

        // The pixels are taken from the icon's own colour bitmap. Asking the shell for a bitmap through
        // GetIconInfo and GetDIBits gives premultiplied BGRA at the icon's size, which is what the XAML
        // imaging types expect.
        ICONINFO iconInfo{};
        if (::GetIconInfo(icon, &iconInfo) == FALSE)
        {
            ::DestroyIcon(icon);
            return Error{ErrorCode::NativeFailure, "the icon's bitmaps could not be read",
                         "ProcessIconReader::Read"};
        }

        IconPixels pixels;
        BITMAP bitmap{};
        if (::GetObjectW(iconInfo.hbmColor, sizeof(bitmap), &bitmap) == 0)
        {
            ::DeleteObject(iconInfo.hbmColor);
            ::DeleteObject(iconInfo.hbmMask);
            ::DestroyIcon(icon);
            return Error{ErrorCode::NativeFailure, "the icon's colour bitmap could not be measured",
                         "ProcessIconReader::Read"};
        }

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = bitmap.bmWidth;
        // Negative height requests a top-down bitmap, which is the row order the imaging types use.
        info.bmiHeader.biHeight = -bitmap.bmHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        HDC const deviceContext = ::GetDC(nullptr);
        pixels.width = static_cast<uint32_t>(bitmap.bmWidth);
        pixels.height = static_cast<uint32_t>(bitmap.bmHeight);
        pixels.bgra.resize(static_cast<size_t>(pixels.width) * pixels.height * 4);

        int const rows = ::GetDIBits(deviceContext, iconInfo.hbmColor, 0, pixels.height, pixels.bgra.data(), &info,
                                     DIB_RGB_COLORS);
        ::ReleaseDC(nullptr, deviceContext);

        ::DeleteObject(iconInfo.hbmColor);
        ::DeleteObject(iconInfo.hbmMask);
        ::DestroyIcon(icon);

        if (rows == 0)
        {
            return Error{ErrorCode::NativeFailure, "the icon's pixels could not be read",
                         "ProcessIconReader::Read"};
        }

        // Icons carry an alpha channel; a fully transparent bitmap means the icon came back as a mask
        // only, which happens for a few shell icons. Treating it as no icon keeps the caller from drawing
        // an invisible row.
        bool anyAlpha = false;
        for (size_t i = 3; i < pixels.bgra.size(); i += 4)
        {
            if (pixels.bgra[i] != 0)
            {
                anyAlpha = true;
                break;
            }
        }

        if (!anyAlpha)
        {
            return Error{ErrorCode::NativeFailure, "the icon has no opaque pixels",
                         "ProcessIconReader::Read"};
        }

        (void)size;
        return pixels;
    }
}
