#include "UI/Views/AboutPage.h"
#include "UI/Theming/Controls.h"
#include "Platform/StoragePaths.h"

namespace tmpp::ui
{
    AboutPage::AboutPage()
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        m_root = Grid();
        m_root.Padding(ThicknessHelper::FromLengths(metrics::CONTENT_LEFT_INSET, 16.0, metrics::PAGE_MARGIN, 16.0));
        ScrollViewer scroller;
        scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        auto body = controls::MakeStack(20.0);
        body.Children().Append(controls::MakeHeading(L"About", 22.0));

        auto identity = controls::MakeCard();
        auto details = controls::MakeStack(10.0);
        auto name = controls::MakeRow(12.0);
        FontIcon icon;
        icon.Glyph(L"\xE9D9");
        icon.FontSize(28.0);
        name.Children().Append(icon);
        name.Children().Append(controls::MakeHeading(L"Task Manager++", 24.0));
        details.Children().Append(name);
        auto version = platform::ApplicationVersion();
        auto versionLabel = controls::MakeText(L"", 14.0, true);
        versionLabel.Text(Localize("Version " + (version.empty() ? std::string{"Unknown"} : version)));
        details.Children().Append(versionLabel);
        details.Children().Append(controls::MakeText(L"A lightweight task manager for Windows.", 14.0));
        identity.Child(details);
        body.Children().Append(identity);

        auto links = controls::MakeCard();
        auto linkContent = controls::MakeStack(8.0);
        linkContent.Children().Append(controls::MakeHeading(L"Project", 16.0));
        HyperlinkButton source;
        source.Content(winrt::box_value(Localize(L"Source code on GitHub")));
        source.NavigateUri(winrt::Windows::Foundation::Uri(L"https://github.com/g122622/TaskManagerPlusPlus"));
        source.HorizontalAlignment(HorizontalAlignment::Left);
        linkContent.Children().Append(source);
        linkContent.Children().Append(controls::MakeText(L"MIT License", 14.0, true));
        HyperlinkButton license;
        license.Content(winrt::box_value(Localize(L"View license")));
        license.NavigateUri(winrt::Windows::Foundation::Uri(L"https://github.com/g122622/TaskManagerPlusPlus/blob/main/LICENSE"));
        license.HorizontalAlignment(HorizontalAlignment::Left);
        linkContent.Children().Append(license);
        links.Child(linkContent);
        body.Children().Append(links);
        scroller.Content(body);
        m_root.Children().Append(scroller);
    }
}
