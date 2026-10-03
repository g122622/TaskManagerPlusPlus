#include "WindowsString.h"

#include <windows.h>

namespace tmpp::platform
{
    std::string ToUtf8(wchar_t const* wide, size_t length)
    {
        if (wide == nullptr || length == 0)
        {
            return {};
        }

        // WideCharToMultiByte needs an int length; the UNICODE_STRING length field
        // is a USHORT so this cannot overflow for the inputs we pass.
        auto const wideLength = static_cast<int>(length);

        int const required = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide, wideLength, nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            return {};
        }

        std::string result(static_cast<size_t>(required), '\0');
        int const written = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide, wideLength, result.data(), required, nullptr, nullptr);
        if (written <= 0)
        {
            return {};
        }

        return result;
    }

    std::string ToUtf8(std::wstring_view wide)
    {
        return ToUtf8(wide.data(), wide.size());
    }

    std::wstring ToUtf16(std::string_view utf8)
    {
        if (utf8.empty())
        {
            return {};
        }

        auto const byteLength = static_cast<int>(utf8.size());

        int const required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), byteLength, nullptr, 0);
        if (required <= 0)
        {
            return {};
        }

        std::wstring result(static_cast<size_t>(required), L'\0');
        int const written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), byteLength, result.data(), required);
        if (written <= 0)
        {
            return {};
        }

        return result;
    }
}