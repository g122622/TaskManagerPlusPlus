#pragma once

#include "Core/Settings.h"
#include <string>
#include <string_view>

namespace tmpp::core::localization {
// Source messages are stable lookup keys. All translations live in assets/locales/*.json.
void Initialize(std::string const& resourceDirectory, LanguageMode language);
void SetLanguage(LanguageMode language);
[[nodiscard]] std::string Translate(std::string_view source);
[[nodiscard]] std::wstring Translate(std::wstring_view source);
[[nodiscard]] std::string Locale();
} // namespace tmpp::core::localization
