#include "ThemeVerification.h"
#include "App/MainWindow.h"
#include "UI/Lists/PriorityIndicator.h"
#include "UI/Theming/Controls.h"

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

namespace tmpp::ui::test
{
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;

    namespace
    {
        int exitCode = 1;
        std::filesystem::path OutputDirectory()
        {
            wchar_t executable[32768]{};
            GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
            return std::filesystem::path{executable}.parent_path();
        }

        template<typename T>
        std::vector<T> Descendants(DependencyObject const& root)
        {
            std::vector<T> result;
            if (auto const value = root.try_as<T>()) result.push_back(value);
            int const count = VisualTreeHelper::GetChildrenCount(root);
            for (int i = 0; i < count; ++i)
            {
                auto children = Descendants<T>(VisualTreeHelper::GetChild(root, i));
                result.insert(result.end(), children.begin(), children.end());
            }
            return result;
        }
    }

    // Runs the actual shell with no saved settings, process rules, sampling or mutations.
    struct WindowVerification : std::enable_shared_from_this<WindowVerification>
    {
        winrt::com_ptr<MainWindow> window;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
        winrt::event_token tick{};
        int phase{0};
        ColorPicker openedPicker{nullptr};
        Flyout openedFlyout{nullptr};
        nlohmann::json report{{"failures", nlohmann::json::array()}, {"phases", nlohmann::json::array()}};

        void Expect(bool condition, char const* message)
        {
            if (!condition) report["failures"].push_back(message);
        }

        winrt::Windows::Foundation::Rect Bounds(FrameworkElement const& element)
        {
            return element.TransformToVisual(window->m_rootGrid).TransformBounds({0, 0,
                static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight())});
        }

        void CheckChrome()
        {
            window->m_rootGrid.UpdateLayout();
            window->_updateNativeDragRegions();
            auto const source = window->m_nonClientPointerSource;
            Expect(source != nullptr, "Native non-client pointer source was not created");
            if (source == nullptr) return;
            auto const scale = window->m_rootGrid.XamlRoot().RasterizationScale();
            auto contains = [scale](auto const& rectangles, double x, double y) {
                for (auto const& rect : rectangles)
                    if (x * scale >= rect.X && x * scale < rect.X + rect.Width &&
                        y * scale >= rect.Y && y * scale < rect.Y + rect.Height) return true;
                return false;
            };
            using winrt::Microsoft::UI::Input::NonClientRegionKind;
            auto const captions = source.GetRegionRects(NonClientRegionKind::Caption);
            auto const passthrough = source.GetRegionRects(NonClientRegionKind::Passthrough);
            auto const brand = Bounds(window->m_brandName);
            double const bx = brand.X + brand.Width / 2.0;
            double const by = brand.Y + brand.Height / 2.0;
            if (window->m_brandName.Visibility() == Visibility::Visible)
            {
                Expect(contains(captions, bx, by) && !contains(passthrough, bx, by),
                    "Title text must support native first-press window dragging");
                auto rects = [](auto const& values) {
                    auto result = nlohmann::json::array();
                    for (auto const& value : values) result.push_back({value.X, value.Y, value.Width, value.Height});
                    return result;
                };
                report["chrome"].push_back({{"brand", {bx, by}}, {"scale", scale},
                    {"captions", rects(captions)}, {"passthrough", rects(passthrough)}});
            }
            Expect(contains(captions, 20, 300) && !contains(passthrough, 20, 300),
                "Empty navigation rail must support native window dragging");
            for (auto const& item : window->m_navigation.MenuItems())
            {
                auto const bounds = Bounds(item.as<FrameworkElement>());
                Expect(contains(passthrough, bounds.X + bounds.Width / 2, bounds.Y + bounds.Height / 2),
                    "A navigation item was covered by the native caption region");
            }
            for (auto const& child : window->m_captionActions.Children())
            {
                auto const bounds = Bounds(child.as<FrameworkElement>());
                Expect(contains(passthrough, bounds.X + bounds.Width / 2, bounds.Y + bounds.Height / 2),
                    "A title bar action was covered by the native caption region");
            }
            auto const actions = Bounds(window->m_captionActions);
            if (window->m_searchBox.Visibility() == Visibility::Visible)
            {
                auto const search = Bounds(window->m_searchBox);
                Expect(std::abs(actions.X - search.X - search.Width - 16.0) < 1.0,
                    "Search box must stretch across the available title bar space");
            }
            auto const captionLeft = window->m_rootGrid.ActualWidth() - window->AppWindow().TitleBar().RightInset() / scale;
            Expect(std::abs(actions.X + actions.Width - captionLeft) < 1.0,
                "A blank strip remains between title bar actions and system buttons");
            report["phases"].push_back({{"phase", phase}, {"actionGap", captionLeft - actions.X - actions.Width}});
            auto const card = window->m_processesView->m_detailCard.Background().as<SolidColorBrush>().Color();
            report["phases"].back()["cardColor"] = {card.A, card.R, card.G, card.B};
            report["phases"].back()["dark"] = window->m_rootGrid.ActualTheme() == ElementTheme::Dark;
        }

        void SeedRows()
        {
            auto& view = *window->m_processesView;
            view.SetSidebarLayout(380, true);
            view.m_snapshot.version = 1;
            int const priorities[]{24, 13, 10, 8, 6, 4};
            for (uint32_t i = 0; i < std::size(priorities); ++i)
            {
                domain::ProcessView process;
                process.identity.pid = 1000 + i;
                process.imageName = "Preview process " + std::to_string(i + 1) + ".exe";
                process.basePriority = priorities[i];
                process.cpuPercent = 60.0 - i * 8;
                process.memory.workingSetSize = (160 + i * 24) * 1024 * 1024;
                if (i == 0) { process.imageName = "Memory Compression"; process.memory.workingSetSize = 9300ull * 1024 * 1024; }
                if (i == 1) process.memory.workingSetSize = 1300ull * 1024 * 1024;
                process.ratesUnavailable = false;
                process.networkRatesUnavailable = false;
                process.networkReceiveBytesPerSec = 2048 * i;
                view.m_snapshot.processes.push_back(std::move(process));
            }
            view.m_listModel->Update(view.m_snapshot, view.m_query);
            view._updateColumnMaxima();
            Expect(view.m_columnMaxima[3] == 1300.0 * 1024 * 1024,
                "Memory Compression still dominates the memory column heat scale");
            Expect(view._heatBrush(3, 9300.0 * 1024 * 1024) == view._heatBrush(3, 1300.0 * 1024 * 1024),
                "Rank-one Memory Compression must have the same fill as rank two");
            view._updateHeaderAggregates();
            view.m_rowHost->SetRowCount(static_cast<uint32_t>(std::size(priorities)),
                [&view] { return view._createRow(); },
                [&view](auto const& row, uint32_t index) { view._bindRow(row, index); });
            // Query only this test process's settings; none of the action buttons are invoked.
            view.m_detailPanel->SetProcess(GetCurrentProcessId(), 0, "tmpp.exe");
        }

        void CheckColumns()
        {
            auto& view = *window->m_processesView;
            Expect(view.m_rowHost->LiveRowCount() == 6, "Synthetic table rows were not realised");
            for (auto const& child : view.m_tableHeader.Children())
            {
                auto const header = child.as<FrameworkElement>();
                auto const logical = winrt::unbox_value<uint32_t>(header.Tag());
                Expect(Grid::GetColumn(header) == static_cast<int32_t>(view.m_columnPositions[logical]), "Header did not follow reordered columns");
                for (auto const& [index, element] : view.m_rowHost->LiveRows())
                {
                    auto const row = element.as<Grid>();
                    auto const cell = row.Children().GetAt(logical).as<FrameworkElement>();
                    Expect(Grid::GetColumn(cell) == Grid::GetColumn(header), "Body cell and header use different column positions");
                    if (cell.Visibility() == Visibility::Visible)
                    {
                        // Borders have a one-pixel margin; compare the actual column boundary.
                        double const cellLeft = Bounds(cell).X - cell.Margin().Left;
                        double const headLeft = Bounds(header).X - header.Margin().Left;
                        Expect(std::abs(cellLeft - headLeft) < 1, "Header and body boundaries are misaligned");
                    }
                    auto const priority = row.Children().GetAt(7).as<Border>().Child().as<StackPanel>();
                    auto const dot = priority.Children().GetAt(0).as<winrt::Microsoft::UI::Xaml::Shapes::Ellipse>();
                    auto const actual = dot.Fill().as<SolidColorBrush>().Color();
                    auto const expected = PriorityIndicatorColor(view.m_snapshot.processes[index].basePriority);
                    Expect(actual.R == expected.red && actual.G == expected.green && actual.B == expected.blue,
                        "Priority indicator colour does not match the process's priority");
                    auto const dotBounds = Bounds(dot);
                    auto const labelBounds = Bounds(priority.Children().GetAt(1).as<FrameworkElement>());
                    Expect(std::abs(dotBounds.Y + dotBounds.Height / 2 - labelBounds.Y - labelBounds.Height / 2) < 1,
                        "Priority dot is not vertically centred with its label");
                }
            }
            double boundary = 12.0;
            for (auto const column : view.m_columnOrder)
            {
                if (!view.m_columnVisible[column]) continue;
                boundary += view.m_columnWidths[column];
                auto const handle = view.m_columnHandles[column]->Element();
                if (handle.Visibility() == Visibility::Visible)
                    Expect(std::abs(Canvas::GetLeft(handle) + handle.Width() / 2 - boundary) < 0.01,
                        "Resize divider did not follow the reordered column boundary");
            }
            int applyCount = 0;
            for (auto const& button : Descendants<Button>(view.m_detailPanel->Root()))
            {
                if (auto const text = button.Content().try_as<winrt::Windows::Foundation::IPropertyValue>();
                    text != nullptr && text.Type() == winrt::Windows::Foundation::PropertyType::String && text.GetString() == L"Apply")
                {
                    ++applyCount;
                    Expect(button.Margin().Left >= 8, "Priority combobox and Apply button need an explicit gap");
                }
            }
            Expect(applyCount == 4, "Priority controls were not realised");
        }

        winrt::Windows::Foundation::IAsyncAction Capture(wchar_t const* name, UIElement const& element = nullptr)
        {
            using namespace winrt::Windows::Storage;
            using namespace winrt::Windows::Graphics::Imaging;
            winrt::Microsoft::UI::Xaml::Media::Imaging::RenderTargetBitmap bitmap;
            co_await bitmap.RenderAsync(element != nullptr ? element : window->m_rootGrid);
            auto const pixels = co_await bitmap.GetPixelsAsync();
            std::vector<uint8_t> bytes(pixels.Length());
            Streams::DataReader::FromBuffer(pixels).ReadBytes(bytes);
            auto const folder = co_await StorageFolder::GetFolderFromPathAsync(OutputDirectory().wstring());
            auto const file = co_await folder.CreateFileAsync(name, CreationCollisionOption::ReplaceExisting);
            auto const stream = co_await file.OpenAsync(FileAccessMode::ReadWrite);
            auto const encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream);
            encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied,
                bitmap.PixelWidth(), bitmap.PixelHeight(), 96, 96, bytes);
            co_await encoder.FlushAsync();
        }

        void Finish()
        {
            timer.Stop();
            timer.Tick(tick);
            report["passed"] = report["failures"].empty();
            exitCode = report["failures"].empty() ? 0 : 1;
            std::ofstream(OutputDirectory() / "ui-window-verification.json") << report.dump(2) << '\n';
            window->Close();
            Application::Current().Exit();
        }

        winrt::fire_and_forget Advance()
        {
            auto lifetime = shared_from_this();
            timer.Stop();
            try
            {
                CheckChrome();
                if (phase == 0)
                {
                    SeedRows();
                    window->m_processesView->SetColumnOrder({7, 0, 2, 3, 1, 5, 4, 6, 8});
                    window->m_startedAt = std::chrono::steady_clock::now() - std::chrono::seconds(3661);
                    window->_updateStatusBar();
                    Expect(std::wstring_view{window->m_statusText.Text()}.find(L"01:01:01") != std::wstring_view::npos,
                        "Status bar did not show elapsed application uptime");
                }
                else if (phase == 1)
                {
                    CheckColumns();
                    co_await Capture(L"ui-window-light.png");
                    auto settings = window->m_currentSettings;
                    settings.theme = core::ThemeMode::Dark;
                    window->_applySettings(settings);
                }
                else if (phase == 2)
                {
                    CheckColumns();
                    co_await Capture(L"ui-window-dark.png");
                    window->m_navigation.SelectedItem(window->m_navigation.FooterMenuItems().GetAt(1));
                }
                else if (phase == 3)
                {
                    Expect(window->m_aboutPage != nullptr, "About navigation did not create the About page");
                    auto const settings = Bounds(window->m_navigation.FooterMenuItems().GetAt(0).as<FrameworkElement>());
                    auto const about = Bounds(window->m_navigation.FooterMenuItems().GetAt(1).as<FrameworkElement>());
                    Expect(about.Y >= settings.Y + settings.Height, "About entry must sit directly below Settings");
                    bool version = false;
                    for (auto const& text : Descendants<TextBlock>(window->m_contentHost))
                        if (std::wstring_view{text.Text()}.starts_with(L"Version ")) version = true;
                    Expect(version, "About page is missing its executable version");
                    co_await Capture(L"ui-window-about.png");
                    window->m_navigation.SelectedItem(window->m_navigation.FooterMenuItems().GetAt(0));
                }
                else if (phase == 4)
                {
                    int samplingControls = 0;
                    for (auto const& number : Descendants<NumberBox>(window->m_contentHost))
                    {
                        auto const id = Automation::AutomationProperties::GetAutomationId(number);
                        if (id == L"SamplingInterval") { number.Value(750); ++samplingControls; }
                        if (id == L"HistoryDuration") { number.Value(135); ++samplingControls; }
                    }
                    Expect(samplingControls == 2, "Settings must expose both sampling interval and history duration");
                    Expect(window->m_coordinator->IntervalMs() == 750 && window->m_coordinator->HistorySeconds() == 135,
                        "Sampling settings did not reach the live coordinator");
                    co_await Capture(L"ui-window-settings.png");
                    for (auto const& button : Descendants<Button>(window->m_contentHost))
                    {
                        auto const flyout = button.Flyout().try_as<Flyout>();
                        if (flyout == nullptr) continue;
                        auto const picker = flyout.Content().try_as<ColorPicker>();
                        if (picker == nullptr) continue;
                        openedFlyout = flyout;
                        openedPicker = picker;
                        flyout.ShowAt(button);
                        break;
                    }
                    Expect(openedPicker != nullptr, "Colour picker flyout was not found");
                }
                else if (phase == 5)
                {
                    if (openedPicker != nullptr)
                    {
                        Expect(!openedPicker.IsMoreButtonVisible() && openedPicker.IsColorChannelTextInputVisible() && openedPicker.IsHexInputVisible(),
                            "Colour picker must expose channel and hex input without a More button");
                        Expect(openedPicker.ActualTheme() == window->m_rootGrid.ActualTheme(),
                            "Colour picker flyout did not follow the window's theme");
                        for (auto const& element : Descendants<FrameworkElement>(openedPicker))
                        {
                            if (element.Name() == L"MoreButton") Expect(element.Visibility() == Visibility::Collapsed, "More button is still visible");
                            if (element.Name() == L"TextEntryGrid") Expect(element.Visibility() == Visibility::Visible, "Colour input is still collapsed");
                        }
                        co_await Capture(L"ui-color-picker.png", openedPicker);
                        openedFlyout.Hide();
                    }
                    bool selectedLanguage = false;
                    for (auto const& combo : Descendants<ComboBox>(window->m_contentHost))
                        if (Automation::AutomationProperties::GetName(combo) == ui::Localize(L"Language"))
                        {
                            combo.SelectedIndex(static_cast<int32_t>(core::LanguageMode::SimplifiedChinese));
                            selectedLanguage = true;
                            break;
                        }
                    Expect(selectedLanguage, "Language setting was not found");
                    co_await Capture(L"ui-window-chinese.png");
                    window->AppWindow().Resize({480, 760});
                }
                else if (phase == 6)
                {
                    window->m_navigation.SelectedItem(window->m_navigation.MenuItems().GetAt(0));
                    window->m_rootGrid.UpdateLayout();
                    bool translatedPriority = false;
                    report["locale"] = core::localization::Locale();
                    report["priorityTranslation"] = winrt::to_string(ui::Localize(L"Priority"));
                    for (auto const& text : Descendants<TextBlock>(window->m_contentHost))
                    {
                        if (text.Text() == ui::Localize(L"Priority")) translatedPriority = true;
                    }
                    Expect(ui::Localize(L"Priority") != L"Priority" && translatedPriority,
                        "A cached process page retained its old language after returning from Settings");
                    wchar_t const* priorities[]{L"Realtime", L"High", L"Above normal", L"Normal", L"Below normal", L"Low"};
                    for (auto const& [index, element] : window->m_processesView->m_rowHost->LiveRows())
                    {
                        auto const content = element.as<Grid>().Children().GetAt(7).as<Border>().Child().as<StackPanel>();
                        Expect(content.Children().GetAt(1).as<TextBlock>().Text() == ui::Localize(priorities[index]),
                            "Language switch skipped a realised process row");
                    }
                    bool translatedSidebar = false;
                    for (auto const& text : Descendants<TextBlock>(window->m_processesView->m_detailPanel->Root()))
                        if (text.Text() == ui::Localize(L"Priority")) translatedSidebar = true;
                    Expect(translatedSidebar, "Language switch skipped the cached process sidebar");
                    Expect(window->m_processesView->m_detailCard.Visibility() == Visibility::Collapsed,
                        "A narrow window must collapse the process sidebar before it clips controls");
                    Expect(window->m_searchBox.Visibility() == Visibility::Collapsed,
                        "Switching pages in a narrow window restored an overlapping search box");
                    co_await Capture(L"ui-window-narrow.png");
                }
                else
                {
                    Finish();
                    co_return;
                }
                ++phase;
                timer.Start();
            }
            catch (winrt::hresult_error const& error)
            {
                report["failures"].push_back(winrt::to_string(error.message()));
                Finish();
            }
        }
    };

    Window StartWindowVerification()
    {
        auto state = std::make_shared<WindowVerification>();
        state->window = winrt::make_self<MainWindow>();
        state->window->Title(L"TaskManagerPlusPlus Window Verification");
        state->window->AppWindow().Resize({1280, 900});
        state->window->AppWindow().Show(false);
        state->window->Content().as<FrameworkElement>().UpdateLayout();
        state->timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        state->timer.Interval(std::chrono::milliseconds(250));
        state->tick = state->timer.Tick([state](auto const&, auto const&) { state->Advance(); });
        state->timer.Start();
        return state->window.as<Window>();
    }

    int WindowVerificationExitCode() { return exitCode; }
}
