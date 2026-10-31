#include "ThemeVerification.h"
#include "UI/Charts/HistoryChart.h"
#include "UI/Theming/Controls.h"

#include <filesystem>
#include <cmath>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

namespace tmpp::ui::test
{
    namespace
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        using namespace winrt::Microsoft::UI::Xaml::Media;
        bool enabled = false;
        bool windowEnabled = false;
        int exitCode = 1;

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

        struct Verification
        {
            Window window;
            Grid root;
            ToggleSwitch toggle;
            SolidColorBrush page{nullptr};
            SolidColorBrush text{nullptr};
            SolidColorBrush grid{nullptr};
            std::unique_ptr<HistoryChart> chart;
            winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
            winrt::event_token tick{};
            int phase{0};
            nlohmann::json report{{"failures", nlohmann::json::array()}, {"phases", nlohmann::json::array()}};

            void Expect(bool condition, char const* message)
            {
                if (!condition) report["failures"].push_back(message);
            }

            void SetTheme(ElementTheme theme)
            {
                root.RequestedTheme(theme);
                controls::SetTheme(theme);
                root.UpdateLayout();
            }

            void VerifyTheme()
            {
                bool const light = root.ActualTheme() == ElementTheme::Light;
                auto const color = page.Color();
                Expect(color.A == 255, "Page background must resolve to an opaque native colour");
                Expect(light ? color.R > 200 : color.R < 96, "Background did not follow the requested theme");
                Expect(std::abs(text.Opacity() - 1.0) < 0.000001 && std::abs(grid.Opacity() - 0.08) < 0.000001,
                    "Changing a grid brush must not fade text or native controls");
                nlohmann::json snapshot{{"theme", light ? "light" : "dark"},
                    {"page", {color.A, color.R, color.G, color.B}}, {"textBrushOpacity", text.Opacity()}, {"gridBrushOpacity", grid.Opacity()}};
                bool foundKnob = false;
                for (auto const& shape : Descendants<winrt::Microsoft::UI::Xaml::Shapes::Rectangle>(toggle))
                {
                    if (shape.Name() != L"SwitchKnobOff") continue;
                    auto const fill = shape.Fill().try_as<SolidColorBrush>();
                    if (fill == nullptr) continue;
                    auto const knob = fill.Color();
                    foundKnob = true;
                    snapshot["offKnob"] = {knob.A, knob.R, knob.G, knob.B};
                    if (!light) Expect(knob.R == 255 && knob.G == 255 && knob.B == 255,
                        "The dark off-state switch knob must be white");
                    else Expect(knob.R < 128 && knob.G < 128 && knob.B < 128,
                        "The light off-state switch knob must return to a dark colour");
                    Expect(fill.Opacity() > 0.9, "Native switch knob brush opacity was corrupted");
                }
                Expect(foundKnob, "Native switch template was not realised");
                auto const lines = Descendants<winrt::Microsoft::UI::Xaml::Shapes::Polyline>(chart->Root());
                Expect(lines.size() == 2, "Expected primary and kernel chart lines");
                if (lines.size() == 2)
                {
                    Expect(lines[0].StrokeThickness() == 2.0, "Primary line width was not applied");
                    Expect(lines[1].StrokeThickness() == 3.0, "Primary width overwrote the kernel width");
                    Expect(lines[0].Points().Size() > 0 && lines[0].Points().GetAt(0).Y == 0.0f,
                        "A CPU reading of 100 percent must touch the plot's top edge");
                    auto const points = lines[1].Points();
                    if (points.Size() != 0)
                    {
                        double const bottom = lines[1].Parent().as<Canvas>().ActualHeight();
                        Expect(points.GetAt(points.Size() - 1).Y + lines[1].StrokeThickness() / 2.0 <= bottom + 0.001,
                            "A thick kernel line escaped the plot at zero utilisation");
                    }
                    auto const kernel = lines[1].Stroke().as<SolidColorBrush>().Color();
                    Expect(kernel.R == 242 && kernel.G == 139 && kernel.B == 130,
                        "Kernel colour was not applied independently");
                }
                report["phases"].push_back(std::move(snapshot));
            }

            void Finish()
            {
                timer.Stop();
                timer.Tick(tick);
                exitCode = report["failures"].empty() ? 0 : 1;
                report["passed"] = exitCode == 0;
                wchar_t executable[32768]{};
                GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
                auto const destination = std::filesystem::path{executable}.parent_path() / "ui-theme-verification.json";
                std::ofstream(destination, std::ios::binary) << report.dump(2) << '\n';
                window.Close();
                Application::Current().Exit();
            }

            void Advance()
            {
                try
                {
                    VerifyTheme();
                    if (phase == 0) SetTheme(ElementTheme::Dark);
                    else if (phase == 1) SetTheme(ElementTheme::Light);
                    else if (phase == 2)
                    {
                        window.SystemBackdrop(MicaBackdrop());
                        root.Background(SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
                    }
                    else if (phase == 3)
                    {
                        Expect(window.SystemBackdrop() != nullptr, "Native Mica backdrop could not be enabled");
                        window.SystemBackdrop(nullptr);
                        root.Background(page);
                    }
                    else
                    {
                        Expect(window.SystemBackdrop() == nullptr, "Native backdrop could not be disabled");
                        Finish();
                        return;
                    }
                    ++phase;
                }
                catch (winrt::hresult_error const& error)
                {
                    report["failures"].push_back(winrt::to_string(error.message()));
                    Finish();
                }
            }
        };
    }

    void EnableThemeVerification() { enabled = true; }
    bool ThemeVerificationEnabled() { return enabled; }
    int ThemeVerificationExitCode() { return exitCode; }
    void EnableWindowVerification() { windowEnabled = true; }
    bool WindowVerificationEnabled() { return windowEnabled; }

    Window StartThemeVerification()
    {
        auto state = std::make_shared<Verification>();
        state->window.Title(L"TaskManagerPlusPlus Theme Verification");
        state->window.AppWindow().Resize({760, 440});
        state->root.RowDefinitions().Append(controls::MakeAutoRow());
        state->root.RowDefinitions().Append(controls::MakeStarRow());
        state->root.Padding(ThicknessHelper::FromUniformLength(24));
        state->SetTheme(ElementTheme::Light);
        state->page = controls::ThemedBrush(theme::PAGE_BACKGROUND).as<SolidColorBrush>();
        state->text = controls::ThemedBrush(theme::SUBTLE_TEXT).as<SolidColorBrush>();
        state->grid = controls::ThemedBrush(theme::SUBTLE_TEXT).as<SolidColorBrush>();
        state->grid.Opacity(0.08);
        state->root.Background(state->page);
        controls::ConfigureThemeResources(state->root);
        state->toggle.MinWidth(0);
        state->toggle.Width(40);
        state->toggle.IsOn(false);
        state->toggle.OnContent(winrt::box_value(L""));
        state->toggle.OffContent(winrt::box_value(L""));
        state->root.Children().Append(state->toggle);
        state->chart = std::make_unique<HistoryChart>(L"CPU", winrt::Windows::UI::Color{255, 76, 194, 255}, 100.0);
        state->chart->SetSecondaryLineColor({255, 242, 139, 130});
        state->chart->SetSecondaryLineWidth(3.0);
        state->chart->SetLineWidth(2.0);
        state->chart->SetSeries({{100, 25, 40, 15}, 4});
        state->chart->SetSecondarySeries({{4, 12, 20, 0}, 4});
        Grid::SetRow(state->chart->Root(), 1);
        state->root.Children().Append(state->chart->Root());
        state->window.Content(state->root);
        state->window.AppWindow().Show(false);
        state->window.AppWindow().Hide();
        state->toggle.ApplyTemplate();
        state->root.UpdateLayout();
        state->timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        state->timer.Interval(std::chrono::milliseconds(100));
        state->tick = state->timer.Tick([state](auto const&, auto const&) { state->Advance(); });
        state->timer.Start();
        return state->window;
    }
}
