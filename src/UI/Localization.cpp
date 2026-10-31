#include "UI/Localization.h"
#include "UI/WinRTUI.h"
#include <unordered_map>

namespace tmpp::ui {
namespace {
bool refreshing = false;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

winrt::Windows::Foundation::IInspectable TranslateValue(winrt::Windows::Foundation::IInspectable const& value)
{
    if (auto const property = value.try_as<winrt::Windows::Foundation::IPropertyValue>();
        property != nullptr && property.Type() == winrt::Windows::Foundation::PropertyType::String)
        return winrt::box_value(Localize(property.GetString()));
    return value;
}

using Visited = std::unordered_map<void*, DependencyObject>;

void Visit(DependencyObject const& object, Visited& visited)
{
    if (object == nullptr) return;
    // IDependencyObject can be a temporary COM tear-off; its address may be recycled for another
    // element. Use canonical IUnknown identity and retain each object until the walk finishes.
    auto const identity = object.as<::IUnknown>();
    if (!visited.emplace(identity.get(), object).second) return;
    if (auto const text = object.try_as<TextBlock>()) text.Text(Localize(text.Text()));
    if (auto const box = object.try_as<TextBox>()) box.PlaceholderText(Localize(box.PlaceholderText()));
    if (auto const content = object.try_as<ContentControl>()) content.Content(TranslateValue(content.Content()));
    if (auto const item = object.try_as<MenuFlyoutItem>()) item.Text(Localize(item.Text()));
    if (auto const submenu = object.try_as<MenuFlyoutSubItem>()) {
        submenu.Text(Localize(submenu.Text()));
        for (auto const& item : submenu.Items())
            Visit(item, visited);
    }
    if (auto const dialog = object.try_as<ContentDialog>()) {
        dialog.Title(TranslateValue(dialog.Title()));
        dialog.PrimaryButtonText(Localize(dialog.PrimaryButtonText()));
        dialog.SecondaryButtonText(Localize(dialog.SecondaryButtonText()));
        dialog.CloseButtonText(Localize(dialog.CloseButtonText()));
    }
    if (auto const combo = object.try_as<ComboBox>()) {
        auto const index = combo.SelectedIndex();
        for (uint32_t i = 0; i < combo.Items().Size(); ++i) {
            auto const value = combo.Items().GetAt(i);
            auto const translated = TranslateValue(value);
            if (auto const property = value.try_as<winrt::Windows::Foundation::IPropertyValue>(); property != nullptr &&
                property.Type() == winrt::Windows::Foundation::PropertyType::String &&
                property.GetString() != winrt::unbox_value<winrt::hstring>(translated))
                combo.Items().SetAt(i, translated);
        }
        combo.SelectedIndex(index);
    }
    auto visitFlyout = [&visited](Controls::Primitives::FlyoutBase const& flyout) {
        if (auto const menu = flyout.try_as<MenuFlyout>())
            for (auto const& item : menu.Items())
                Visit(item, visited);
        if (auto const content = flyout.try_as<Flyout>()) Visit(content.Content(), visited);
    };
    if (auto const element = object.try_as<FrameworkElement>()) {
        visitFlyout(element.ContextFlyout());
        auto const tooltip = ToolTipService::GetToolTip(element);
        if (tooltip != nullptr) ToolTipService::SetToolTip(element, TranslateValue(tooltip));
        auto const name = Automation::AutomationProperties::GetName(element);
        if (!name.empty()) Automation::AutomationProperties::SetName(element, Localize(name));
    }
    if (auto const button = object.try_as<Button>()) visitFlyout(button.Flyout());
    // Pages can be detached or awaiting a template/layout pass during a language switch. Walk their
    // logical content as well, so a cached page does not reappear with its old language.
    if (auto const panel = object.try_as<Panel>())
        for (auto const& child : panel.Children()) Visit(child, visited);
    if (auto const border = object.try_as<Border>()) Visit(border.Child(), visited);
    if (auto const content = object.try_as<ContentControl>())
        Visit(content.Content().try_as<DependencyObject>(), visited);
    if (auto const items = object.try_as<ItemsControl>())
        for (auto const& item : items.Items()) Visit(item.try_as<DependencyObject>(), visited);
    if (auto const navigation = object.try_as<NavigationView>())
    {
        for (auto const& item : navigation.MenuItems()) Visit(item.try_as<DependencyObject>(), visited);
        for (auto const& item : navigation.FooterMenuItems()) Visit(item.try_as<DependencyObject>(), visited);
    }
    auto const count = Media::VisualTreeHelper::GetChildrenCount(object);
    for (int i = 0; i < count; ++i)
        Visit(Media::VisualTreeHelper::GetChild(object, i), visited);
}
} // namespace

bool LocalizationInProgress()
{
    return refreshing;
}
void LocalizeTree(winrt::Microsoft::UI::Xaml::DependencyObject const& root)
{
    if (refreshing) return;
    refreshing = true;
    struct Reset {
        ~Reset() { refreshing = false; }
    } reset;
    Visited visited;
    Visit(root, visited);
}
} // namespace tmpp::ui
