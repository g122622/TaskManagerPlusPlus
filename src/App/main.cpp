// Process entry point for the TaskManagerPlusPlus WinUI 3 application.
//
// Because the project defines no XAML ApplicationDefinition, the WinUI targets
// do not generate an entry point, so it is provided here.
#include "WinRT.h"

#include "App.h"
#include "StartupLog.h"

#include <windows.h>

namespace
{
    void _reportFailure(char const* stage, winrt::hresult_error const& e)
    {
        tmpp::diag::LogStartupError(stage, e.code().value, winrt::to_string(e.message()).c_str());
    }

    /// The main window's title, used to find the running instance.
    ///
    /// The title rather than the window class: WinUI registers its own class for the window it
    /// creates, whose name this code neither controls nor can rely on across versions. The title is
    /// set by this application and is stable.
    constexpr wchar_t const* WINDOW_TITLE = L"TaskManagerPlusPlus";

    /// Name of the mutex that marks the application as running.
    ///
    /// A named mutex in the local namespace rather than the global one: a second instance started by
    /// a different user in the same session is that user's business, and the global namespace would
    /// make one user's launch block another's.
    constexpr wchar_t const* SINGLE_INSTANCE_MUTEX = L"Local\\TaskManagerPlusPlus.SingleInstance";

    /// Brings the running instance's window to the front.
    ///
    /// The original behaves this way, and it matches a user's intent: launching a task manager while
    /// one is open means "show me it", not "run another copy". A machine running two of these is
    /// measuring itself twice, and the second copy's own sampling shows up in the first one's figures.
    ///
    /// @return True when a running instance was found and activated.
    bool _activateRunningInstance()
    {
        HWND const existing = FindWindowW(nullptr, WINDOW_TITLE);
        if (existing == nullptr)
        {
            return false;
        }

        // A minimised window is restored first: SetForegroundWindow on a minimised window does
        // nothing visible, so the user would see no response at all.
        if (IsIconic(existing) != FALSE)
        {
            ShowWindow(existing, SW_RESTORE);
        }

        SetForegroundWindow(existing);
        return true;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    tmpp::diag::LogStartup("--- startup begin ---");

    // Single instance, checked two ways because either alone has a hole.
    //
    // The window is what actually matters -- a second copy is unwanted precisely because there is
    // already a window to show -- so it is checked first and directly. The mutex then closes the race
    // the window check cannot: two launches arriving before either has created a window would both
    // find none and both carry on.
    bool const foundWindow = _activateRunningInstance();

    HANDLE const instanceMutex = CreateMutexW(nullptr, TRUE, SINGLE_INSTANCE_MUTEX);
    bool const mutexExists = (instanceMutex != nullptr) && (GetLastError() == ERROR_ALREADY_EXISTS);

    {
        // Both outcomes are recorded. Which one fired, and whether either did, is otherwise
        // invisible: a missing collision and a check that was never reached look identical from
        // outside, and that is exactly the ambiguity that made this hard to verify.
        std::string const message = std::string{"single instance: window="} + (foundWindow ? "found" : "none") +
                                    " mutex=" + (mutexExists ? "exists" : "new") +
                                    " session=" + std::to_string(static_cast<unsigned long>(
                                                      [] {
                                                          DWORD session = 0;
                                                          ProcessIdToSessionId(GetCurrentProcessId(), &session);
                                                          return session;
                                                      }()));
        tmpp::diag::LogStartup(message.c_str());
    }

    if (foundWindow)
    {
        tmpp::diag::LogStartup("another instance is running; activated its window instead");
        return 0;
    }

    bool const alreadyRunning = mutexExists;

    if (alreadyRunning)
    {
        tmpp::diag::LogStartup("another instance is starting; abandoning this launch");

        // The other instance may not have created its window yet, so there is nothing to activate.
        // Waiting briefly for it is friendlier than exiting silently, which looks like a failure.
        for (int attempt = 0; attempt < 40; ++attempt)
        {
            if (_activateRunningInstance())
            {
                break;
            }
            Sleep(50);
        }

        // The other instance owns the mutex; this handle is only this process's reference to it.
        CloseHandle(instanceMutex);
        return 0;
    }

    // The handle is deliberately not closed: it is what marks this process as the running instance,
    // and the operating system releases it on exit. Closing it here would let the next launch start a
    // second copy.

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
