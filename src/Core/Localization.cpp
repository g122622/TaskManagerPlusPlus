#include "Core/Localization.h"
#include "Platform/FileSystem.h"
#include "Platform/Windows/WindowsString.h"

#include <algorithm>
#include <map>
#include <regex>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace tmpp::core::localization {
namespace {
struct Pattern {
    std::regex expression;
    std::string english;
    std::string translated;
    bool reverse{false};
};
std::string directory;
std::string locale{"en-US"};
std::map<std::string, std::string, std::less<>> messages;
std::map<std::string, std::string, std::less<>> originals;
std::vector<Pattern> patterns;
std::map<std::string, std::string, std::less<>> englishMessages;
std::unordered_map<std::string, std::string> cache;

std::regex CompilePattern(std::string const& source)
{
    std::string expression = "^";
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '{' && i + 2 < source.size() && source[i + 2] == '}' && source[i + 1] >= '0' &&
            source[i + 1] <= '9') {
            expression += "(.*?)";
            i += 2;
        } else {
            if (std::string_view{R"(\.^$|()[]*+?{})"}.find(source[i]) != std::string_view::npos) expression += '\\';
            expression += source[i];
        }
    }
    return std::regex(expression + "$", std::regex::optimize);
}

std::string Substitute(std::string const& text, std::smatch const& captures, std::string const& matchedTemplate)
{
    std::map<std::string, std::string> values;
    size_t templatePosition = 0;
    for (size_t i = 1; i < captures.size(); ++i) {
        templatePosition = matchedTemplate.find('{', templatePosition);
        if (templatePosition == std::string::npos) break;
        values.emplace(matchedTemplate.substr(templatePosition, 3), Translate(std::string_view{captures[i].str()}));
        templatePosition += 3;
    }
    std::string result;
    for (size_t i = 0; i < text.size();) {
        auto const found = text[i] == '{' ? values.find(text.substr(i, 3)) : values.end();
        if (found != values.end()) {
            result += found->second;
            i += 3;
        } else
            result += text[i++];
    }
    return result;
}

} // namespace

void Initialize(std::string const& resourceDirectory, LanguageMode language)
{
    directory = resourceDirectory;
    messages.clear();
    englishMessages.clear();
    cache.clear();
    auto const english = platform::ReadFileToString(directory + "/en-US.json");
    if (english.Success()) {
        auto const data = nlohmann::json::parse(english.Value(), nullptr, false);
        if (data.is_object() && data.contains("messages") && data["messages"].is_object())
            for (auto const& [key, value] : data["messages"].items())
                if (value.is_string()) englishMessages.emplace(key, value.get<std::string>());
    }
    originals.clear();
    patterns.clear();
    auto const file = platform::ReadFileToString(directory + "/zh-CN.json");
    if (file.Success()) {
        auto const root = nlohmann::json::parse(file.Value(), nullptr, false);
        if (root.is_object() && root.contains("messages") && root["messages"].is_object()) {
            for (auto const& [key, value] : root["messages"].items()) {
                if (!value.is_string()) continue;
                auto const translated = value.get<std::string>();
                messages.emplace(key, translated);
                originals.emplace(translated, key);
                if (key.find("{0}") != std::string::npos) {
                    patterns.push_back({CompilePattern(key), key, translated, false});
                    patterns.push_back({CompilePattern(translated), key, translated, true});
                }
            }
            std::stable_sort(patterns.begin(), patterns.end(), [](auto const& a, auto const& b) {
                return a.english.size() > b.english.size();
            });
        }
    }
    SetLanguage(language);
}

void SetLanguage(LanguageMode language)
{
    bool const chinese =
        language == LanguageMode::SimplifiedChinese || (language == LanguageMode::System && platform::UsesChineseUI());
    std::string const selected = chinese ? "zh-CN" : "en-US";
    if (selected != locale) cache.clear();
    locale = selected;
}

std::string Locale()
{
    return locale;
}

std::string Translate(std::string_view source)
{
    bool const chinese = locale == "zh-CN";
    if (auto const found = messages.find(source); found != messages.end())
        return chinese
            ? found->second
            : (englishMessages.contains(source) ? englishMessages.find(source)->second : std::string{source});
    if (auto const found = originals.find(source); found != originals.end())
        return chinese ? std::string{source} : found->second;
    std::string const text{source};
    if (auto const cached = cache.find(text); cached != cache.end()) return cached->second;
    auto remember = [&text](std::string result) {
        if (cache.size() >= 4096) cache.clear();
        cache.emplace(text, result);
        return result;
    };
    for (auto const& pattern : patterns) {
        std::smatch captures;
        if (std::regex_match(text, captures, pattern.expression))
            return remember(Substitute(chinese ? pattern.translated : pattern.english,
                captures,
                pattern.reverse ? pattern.translated : pattern.english));
    }
    return remember(text);
}

std::wstring Translate(std::wstring_view source)
{
    return platform::ToUtf16(Translate(platform::ToUtf8(source)));
}
} // namespace tmpp::core::localization
