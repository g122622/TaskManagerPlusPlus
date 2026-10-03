// Conversions between Windows UTF-16 strings and UTF-8 std::string.
//
// The project works in UTF-8 internally: std::string is cheaper to handle than
// std::wstring in the hot sampling path, and spdlog formats UTF-8 natively.
// Windows APIs hand back UTF-16, so every boundary needs a conversion.
#pragma once

#include <string>
#include <string_view>

namespace tmpp::platform
{
    /**
     * @brief Converts a UTF-16 string to UTF-8.
     *
     * @param wide Source string; may be empty.
     * @return UTF-8 representation, or an empty string on conversion failure.
     */
    [[nodiscard]] std::string ToUtf8(std::wstring_view wide);

    /**
     * @brief Converts a UTF-16 buffer of explicit length to UTF-8.
     *
     * Used for UNICODE_STRING payloads, which are not necessarily
     * null-terminated.
     *
     * @param wide Pointer to the first UTF-16 code unit.
     * @param length Number of UTF-16 code units.
     * @return UTF-8 representation, or an empty string on failure.
     */
    [[nodiscard]] std::string ToUtf8(wchar_t const* wide, size_t length);

    /**
     * @brief Converts a UTF-8 string to UTF-16.
     *
     * @param utf8 Source string; may be empty.
     * @return UTF-16 representation, or an empty string on failure.
     */
    [[nodiscard]] std::wstring ToUtf16(std::string_view utf8);
}