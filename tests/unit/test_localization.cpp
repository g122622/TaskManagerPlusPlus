#include "Core/Localization.h"
#include "Platform/FileSystem.h"
#include <filesystem>
#include <regex>
#include <set>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace tmpp::core::test {
namespace {
std::string ResourceDirectory()
{
    return (std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path() / "assets/locales").string();
}
std::set<std::string> Placeholders(std::string const& text)
{
    std::regex token(R"(\{[0-9]\})");
    std::set<std::string> result;
    for (auto i = std::sregex_iterator(text.begin(), text.end(), token); i != std::sregex_iterator(); ++i)
        result.insert(i->str());
    return result;
}
} // namespace

TEST(LocalizationTest, CataloguesHaveMatchingKeysAndPlaceholders)
{
    auto const english = platform::ReadFileToString(ResourceDirectory() + "/en-US.json");
    auto const chinese = platform::ReadFileToString(ResourceDirectory() + "/zh-CN.json");
    ASSERT_TRUE(english.Success());
    ASSERT_TRUE(chinese.Success());
    auto const en = nlohmann::json::parse(english.Value())["messages"];
    auto const zh = nlohmann::json::parse(chinese.Value())["messages"];
    ASSERT_EQ(en.size(), zh.size());
    for (auto const& [key, value] : en.items()) {
        ASSERT_TRUE(zh.contains(key)) << key;
        EXPECT_EQ(Placeholders(value.get<std::string>()), Placeholders(zh[key].get<std::string>())) << key;
    }
}

TEST(LocalizationTest, LoadsExternalTranslationsAndSwitchesExistingTextBack)
{
    auto const file = platform::ReadFileToString(ResourceDirectory() + "/zh-CN.json");
    ASSERT_TRUE(file.Success());
    auto const messages = nlohmann::json::parse(file.Value())["messages"];
    localization::Initialize(ResourceDirectory(), LanguageMode::SimplifiedChinese);
    auto const translated = localization::Translate(std::string_view{"Settings"});
    EXPECT_EQ(translated, messages["Settings"].get<std::string>());
    EXPECT_NE(translated, "Settings");
    auto const dynamic = localization::Translate(std::string_view{"Showing 4 of 25 processes"});
    EXPECT_NE(dynamic, "Showing 4 of 25 processes");
    auto const processMessage =
        localization::Translate(std::string_view{"This will end sample{1}.exe and every process it started."});
    EXPECT_NE(processMessage.find("sample{1}.exe"), std::string::npos);
    localization::SetLanguage(LanguageMode::English);
    EXPECT_EQ(localization::Translate(translated), "Settings");
    EXPECT_EQ(localization::Translate(dynamic), "Showing 4 of 25 processes");
    EXPECT_EQ(localization::Translate(std::string_view{"unknown.exe"}), "unknown.exe");
}

TEST(LocalizationTest, MissingCatalogueUsesSourceText)
{
    localization::Initialize("missing-locale-directory", LanguageMode::SimplifiedChinese);
    EXPECT_EQ(localization::Translate(std::string_view{"Settings"}), "Settings");
    localization::Initialize(ResourceDirectory(), LanguageMode::English);
}
} // namespace tmpp::core::test
