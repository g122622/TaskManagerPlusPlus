// Process entry point for the TaskManagerPlusPlus WinUI 3 application.
//
// Because the project defines no XAML ApplicationDefinition, the WinUI targets
// do not generate an entry point, so it is provided here.
#include "WinRT.h"

#include "App.h"
#include "StartupLog.h"

namespace
{
    void _reportFailure(char const* stage, winrt::hresult_error const& e)
    {
        tmpp::diag::LogStartupError(stage, e.code().value, winrt::to_string(e.message()).c_str());
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    tmpp::diag::LogStartup("--- startup begin ---");

    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        tmpp::diag::LogStartup("init_apartment ok");
    }
    catch (winrt::hresult_error const& e)
    {
        _reportFailure("init_apartment", e);
        return 1;
    }

    try
    {
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
            tmpp::diag::LogStartup("Application::Start callback entered");

            try
            {
                winrt::make<tmpp::App>();
                tmpp::diag::LogStartup("App constructed ok");
            }
            catch (winrt::hresult_error const& e)
            {
                _reportFailure("App construction", e);
                throw;
            }
        });
        tmpp::diag::LogStartup("Application::Start returned");
    }
    catch (winrt::hresult_error const& e)
    {
        _reportFailure("Application::Start", e);
        return 1;
    }
    catch (std::exception const& e)
    {
        tmpp::diag::LogStartupError("Application::Start (std)", E_FAIL, e.what());
        return 1;
    }

    tmpp::diag::LogStartup("--- startup complete ---");
    return 0;
}