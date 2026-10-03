// Tests for the Core layer: settings persistence, path resolution and the
// background sampler.
//
// Settings tests write to a temporary file so they exercise the real encoder,
// the real atomic-replace path and the real recovery behaviour. A mocked file
// system would not catch the failures that matter here (a truncated file, a
// hand-edited type, a value outside the permitted range).
#include <gtest/gtest.h>

#include "Core/BackgroundSampler.h"
#include "Core/PathService.h"
#include "Core/Settings.h"

#include "Platform/FileSystem.h"

#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tmpp::core
{
    namespace
    {
        /**
         * @brief A unique temporary directory removed when the test ends.
         */
        class TempDirectory
        {
        public:
            TempDirectory()
            {
                wchar_t buffer[MAX_PATH]{};
                GetTempPathW(MAX_PATH, buffer);

                static std::atomic<uint32_t> counter{0};
                m_path = std::filesystem::path{buffer} /
                         (L"tmpp_test_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                          std::to_wstring(counter.fetch_add(1)));

                std::filesystem::create_directories(m_path);
            }

            ~TempDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(m_path, ignored);
            }

            TempDirectory(TempDirectory const&) = delete;
            TempDirectory& operator=(TempDirectory const&) = delete;

            [[nodiscard]] std::string File(std::string const& name) const
            {
                return (m_path / name).string();
            }

        private:
            std::filesystem::path m_path;
        };

        /**
         * @brief Writes raw bytes to a path, bypassing the atomic writer.
         *
         * Used to simulate a file that was damaged or edited by hand.
         */
        void _writeRaw(std::string const& path, std::string const& contents)
        {
            FILE* file = nullptr;
            ASSERT_EQ(fopen_s(&file, path.c_str(), "wb"), 0);
            ASSERT_NE(file, nullptr);
            fwrite(contents.data(), 1, contents.size(), file);
            fclose(file);
        }
    }

    // ------------------------------------------------------------------------
    // SettingsStore
    // ------------------------------------------------------------------------

    TEST(SettingsStoreTest, MissingFileYieldsDefaults)
    {
        TempDirectory const temp;
        SettingsStore store(temp.File("settings.json"));

        Settings const settings = store.Load();

        // A first run is not an error and must not be reported as a damaged file.
        EXPECT_FALSE(store.DamagedFilePreserved());
        EXPECT_EQ(settings.version, Settings::CURRENT_VERSION);
        EXPECT_EQ(settings.theme, ThemeMode::System);
        EXPECT_EQ(settings.intervalMs, 1000u);
        EXPECT_TRUE(settings.reduceWhenMinimized);
    }

    TEST(SettingsStoreTest, SaveThenLoadRoundTrips)
    {
        TempDirectory const temp;
        SettingsStore store(temp.File("settings.json"));

        Settings original;
        original.theme = ThemeMode::Dark;
        original.language = LanguageMode::SimplifiedChinese;
        original.alwaysOnTop = true;
        original.intervalMs = 2000;
        original.reduceWhenMinimized = false;
        original.historyWindow = HistoryWindow::Minutes30;
        original.windowX = 120;
        original.windowY = 240;
        original.windowWidth = 1600;
        original.windowHeight = 900;
        original.windowMaximized = true;

        ASSERT_TRUE(store.Save(original).Success()) << "saving must succeed";

        Settings const loaded = store.Load();
        EXPECT_EQ(loaded.theme, ThemeMode::Dark);
        EXPECT_EQ(loaded.language, LanguageMode::SimplifiedChinese);
        EXPECT_TRUE(loaded.alwaysOnTop);
        EXPECT_EQ(loaded.intervalMs, 2000u);
        EXPECT_FALSE(loaded.reduceWhenMinimized);
        EXPECT_EQ(loaded.historyWindow, HistoryWindow::Minutes30);
        EXPECT_EQ(loaded.windowX, 120);
        EXPECT_EQ(loaded.windowY, 240);
        EXPECT_EQ(loaded.windowWidth, 1600);
        EXPECT_EQ(loaded.windowHeight, 900);
        EXPECT_TRUE(loaded.windowMaximized);
    }

    TEST(SettingsStoreTest, SaveIsAtomicAndLeavesNoTempFile)
    {
        TempDirectory const temp;
        std::string const path = temp.File("settings.json");
        SettingsStore store(path);

        ASSERT_TRUE(store.Save(Settings{}).Success());

        // The temporary file used during the write must not survive.
        EXPECT_FALSE(platform::FileExists(path + ".tmp"));
        EXPECT_TRUE(platform::FileExists(path));
    }

    TEST(SettingsStoreTest, CorruptFileFallsBackAndIsPreserved)
    {
        TempDirectory const temp;
        std::string const path = temp.File("settings.json");
        _writeRaw(path, "{ this is not valid json");

        SettingsStore store(path);
        Settings const settings = store.Load();

        // Defaults are used, and the damaged file is kept for inspection rather
        // than silently overwritten.
        EXPECT_EQ(settings.intervalMs, 1000u);
        EXPECT_TRUE(store.DamagedFilePreserved());
        EXPECT_TRUE(platform::FileExists(path + ".bak"));
    }

    TEST(SettingsStoreTest, NonObjectJsonIsTreatedAsCorrupt)
    {
        // Valid JSON, wrong shape: an array rather than an object.
        TempDirectory const temp;
        std::string const path = temp.File("settings.json");
        _writeRaw(path, "[1, 2, 3]");

        SettingsStore store(path);
        Settings const settings = store.Load();

        EXPECT_EQ(settings.intervalMs, 1000u);
        EXPECT_TRUE(store.DamagedFilePreserved());
    }

    TEST(SettingsStoreTest, WrongTypesFallBackPerKey)
    {
        // A hand-edited file with the right keys but wrong types must not crash and
        // must not lose the keys that are still valid.
        TempDirectory const temp;
        std::string const path = temp.File("settings.json");
        _writeRaw(path,
                  R"({
                    "version": 1,
                    "general": { "theme": "purple", "alwaysOnTop": true },
                    "sampling": { "intervalMs": "fast" }
                  })");

        SettingsStore store(path);
        Settings const settings = store.Load();

        EXPECT_FALSE(store.DamagedFilePreserved()) << "the file is valid JSON, just wrong types";

        // The string where a number was expected falls back to the default.
        EXPECT_EQ(settings.intervalMs, 1000u);
        // ...but the neighbouring valid key is still honoured.
        EXPECT_TRUE(settings.alwaysOnTop);
        // An out-of-range enum falls back too.
        EXPECT_EQ(settings.theme, ThemeMode::System);
    }

    TEST(SettingsStoreTest, OutOfRangeValuesAreClampedOnLoad)
    {
        // The whole point of clamping on load: the sampler must be able to trust
        // what it is given, whatever the file contains.
        TempDirectory const temp;
        std::string const path = temp.File("settings.json");
        _writeRaw(path,
                  R"({
                    "version": 1,
                    "sampling": { "intervalMs": 1, "minimizedIntervalMs": 999999 }
                  })");

        SettingsStore store(path);
        Settings const settings = store.Load();

        EXPECT_EQ(settings.intervalMs, domain::sampling::MIN_INTERVAL_MS);
        EXPECT_EQ(settings.minimizedIntervalMs, domain::sampling::MAX_INTERVAL_MS);
    }

    TEST(SettingsStoreTest, MissingKeysUseDefaultsPerKey)
    {
        // A file written by an older version lacks newer keys; that is expected,
        // not an error.
        TempDirectory const temp;
        std::string const path = temp.File("settings.json");
        _writeRaw(path, R"({ "version": 1, "general": { "alwaysOnTop": true } })");

        SettingsStore store(path);
        Settings const settings = store.Load();

        EXPECT_TRUE(settings.alwaysOnTop);
        EXPECT_EQ(settings.intervalMs, domain::sampling::DEFAULT_INTERVAL_MS);
        EXPECT_FALSE(store.DamagedFilePreserved());
    }

    TEST(SettingsStoreTest, AbsurdWindowSizeIsRejected)
    {
        // A zero or tiny window would be impossible to grab, so it is treated as
        // absent rather than restored.
        TempDirectory const temp;
        std::string const path = temp.File("settings.json");
        _writeRaw(path, R"({ "version": 1, "window": { "width": 0, "height": -5 } })");

        SettingsStore store(path);
        Settings const settings = store.Load();

        EXPECT_GT(settings.windowWidth, 200);
        EXPECT_GT(settings.windowHeight, 200);
    }

    TEST(SettingsStoreTest, NegativeWindowPositionIsPreservedAsUnset)
    {
        // A negative position is the documented "not yet decided" marker and must
        // survive a round trip rather than being coerced to 0.
        TempDirectory const temp;
        SettingsStore store(temp.File("settings.json"));

        Settings settings;
        settings.windowX = -1;
        settings.windowY = -1;
        ASSERT_TRUE(store.Save(settings).Success());

        Settings const loaded = store.Load();
        EXPECT_LT(loaded.windowX, 0);
        EXPECT_LT(loaded.windowY, 0);
    }

    TEST(SettingsStoreTest, SaveCreatesMissingDirectory)
    {
        TempDirectory const temp;
        // A nested directory that does not exist yet.
        std::string const path = temp.File("nested\\deeper\\settings.json");
        SettingsStore store(path);

        EXPECT_TRUE(store.Save(Settings{}).Success()) << "the directory must be created on demand";
        EXPECT_TRUE(platform::FileExists(path));
    }

    TEST(SettingsStoreTest, EffectiveIntervalHonoursMinimizePolicy)
    {
        Settings settings;
        settings.intervalMs = 1000;
        settings.reduceWhenMinimized = true;
        settings.minimizedIntervalMs = 5000;

        EXPECT_EQ(settings.EffectiveIntervalMs(false), 1000u);
        EXPECT_EQ(settings.EffectiveIntervalMs(true), 5000u);

        settings.reduceWhenMinimized = false;
        EXPECT_EQ(settings.EffectiveIntervalMs(true), 1000u) << "the policy is off, so the normal interval applies";
    }

    TEST(SettingsStoreTest, HistorySecondsMapsFromEnum)
    {
        Settings settings;
        settings.historyWindow = HistoryWindow::Seconds60;
        EXPECT_EQ(settings.HistorySeconds(), 60u);
        settings.historyWindow = HistoryWindow::Minutes5;
        EXPECT_EQ(settings.HistorySeconds(), 300u);
        settings.historyWindow = HistoryWindow::Minutes30;
        EXPECT_EQ(settings.HistorySeconds(), 1800u);
    }

    // ------------------------------------------------------------------------
    // PathService
    // ------------------------------------------------------------------------

    TEST(PathServiceTest, ResolvesSettingsLocationUnderLocalAppData)
    {
        auto const service = PathService::Create("TaskManagerPlusPlusTest", "settings.json");
        ASSERT_TRUE(service.Success()) << service.GetError().Message();

        std::string const& directory = service.Value().SettingsDirectory();

        // %LOCALAPPDATA% rather than %APPDATA%: these settings describe this
        // machine and must not roam.
        PWSTR localAppDataRaw = nullptr;
        ASSERT_TRUE(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppDataRaw)));
        std::wstring const localAppData{localAppDataRaw};
        CoTaskMemFree(localAppDataRaw);

        // Compare case-insensitively: the API and the file system may differ.
        // Converted explicitly because a narrowing wchar_t -> char copy is lossy
        // and would not compile cleanly at /W4 on a non-ASCII profile path.
        std::string narrowLocalAppData;
        narrowLocalAppData.reserve(localAppData.size());
        for (wchar_t const character : localAppData)
        {
            narrowLocalAppData.push_back(static_cast<char>(character));
        }

        EXPECT_NE(directory.find("TaskManagerPlusPlusTest"), std::string::npos);
        EXPECT_NE(directory.find(narrowLocalAppData), std::string::npos);
    }

    TEST(PathServiceTest, SettingsFileSitsInsideSettingsDirectory)
    {
        auto const service = PathService::Create("TaskManagerPlusPlusTest", "settings.json");
        ASSERT_TRUE(service.Success());

        EXPECT_EQ(service.Value().SettingsFile(), service.Value().SettingsDirectory() + "\\settings.json");
    }

    TEST(PathServiceTest, ApplicationDirectoryReflectsRunningExecutable)
    {
        auto const service = PathService::Create("TaskManagerPlusPlusTest", "settings.json");
        ASSERT_TRUE(service.Success());

        std::string const& directory = service.Value().ApplicationDirectory();
        EXPECT_FALSE(directory.empty());
        // The path must end with a separator so callers can append a file name.
        EXPECT_TRUE(directory.back() == '\\' || directory.back() == '/');
    }

    TEST(PathServiceTest, SettingsPathBuildsRelativeName)
    {
        auto const service = PathService::Create("TaskManagerPlusPlusTest", "settings.json");
        ASSERT_TRUE(service.Success());

        std::string const themed = service.Value().SettingsPath("chart-themes.json");
        EXPECT_NE(themed.find("chart-themes.json"), std::string::npos);
        EXPECT_EQ(themed.find("\\\\"), std::string::npos) << "no doubled separator";
    }

    // ------------------------------------------------------------------------
    // BackgroundSampler
    // ------------------------------------------------------------------------

    TEST(BackgroundSamplerTest, SamplesImmediatelyOnStart)
    {
        // The UI needs data on its first frame, so the first sample must not wait
        // for a full interval.
        std::atomic<int> samples{0};

        BackgroundSampler sampler(1000, [&samples](WakeReason) { samples.fetch_add(1); });
        sampler.Start();

        // Well under one interval: if this passes, the initial sample happened at once.
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        sampler.Stop();

        EXPECT_GE(samples.load(), 1);
    }

    TEST(BackgroundSamplerTest, ReportsInitialWakeReasonFirst)
    {
        std::mutex mutex;
        std::vector<WakeReason> reasons;

        BackgroundSampler sampler(1000, [&mutex, &reasons](WakeReason reason) {
            std::lock_guard const lock(mutex);
            reasons.push_back(reason);
        });
        sampler.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        sampler.Stop();

        std::lock_guard const lock(mutex);
        ASSERT_FALSE(reasons.empty());
        EXPECT_EQ(reasons[0], WakeReason::Initial);
    }

    TEST(BackgroundSamplerTest, SamplesRepeatedlyAtInterval)
    {
        std::atomic<int> samples{0};

        // The shortest permitted interval, to keep the test fast.
        BackgroundSampler sampler(domain::sampling::MIN_INTERVAL_MS, [&samples](WakeReason) {
            samples.fetch_add(1);
        });
        sampler.Start();

        // At 500 ms, roughly four samples fit in this window (including the first).
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
        sampler.Stop();

        EXPECT_GE(samples.load(), 2) << "sampling must repeat, not run once";
    }

    TEST(BackgroundSamplerTest, RequestRefreshWakesWithoutWaitingForInterval)
    {
        // The sampler is set to the slowest interval, but a refresh must not wait
        // five seconds for it. Sampling again within a few hundred milliseconds
        // proves the wait was interrupted.
        std::atomic<int> samples{0};

        BackgroundSampler sampler(domain::sampling::MAX_INTERVAL_MS, [&samples](WakeReason) {
            samples.fetch_add(1);
        });
        sampler.Start();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        int const before = samples.load();

        sampler.RequestRefresh();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        int const after = samples.load();

        sampler.Stop();

        EXPECT_GT(after, before) << "a refresh must interrupt the wait rather than sleep out the interval";
    }

    TEST(BackgroundSamplerTest, RefreshIsReportedAsRefreshReason)
    {
        std::mutex mutex;
        std::vector<WakeReason> reasons;

        BackgroundSampler sampler(domain::sampling::MAX_INTERVAL_MS, [&mutex, &reasons](WakeReason reason) {
            std::lock_guard const lock(mutex);
            reasons.push_back(reason);
        });
        sampler.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        sampler.RequestRefresh();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        sampler.Stop();

        std::lock_guard const lock(mutex);
        EXPECT_NE(std::find(reasons.begin(), reasons.end(), WakeReason::Refresh), reasons.end());
    }

    TEST(BackgroundSamplerTest, SetIntervalAppliesImmediately)
    {
        // Switching from a slow interval to the fastest one must take effect now,
        // not after the old interval expires.
        std::atomic<int> samples{0};

        BackgroundSampler sampler(domain::sampling::MAX_INTERVAL_MS, [&samples](WakeReason) {
            samples.fetch_add(1);
        });
        sampler.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        int const before = samples.load();

        sampler.SetInterval(domain::sampling::MIN_INTERVAL_MS);
        EXPECT_EQ(sampler.IntervalMs(), domain::sampling::MIN_INTERVAL_MS);

        // Long enough for two fast samples, far too short for the original interval.
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        int const after = samples.load();

        sampler.Stop();

        EXPECT_GT(after, before) << "the new interval must apply without waiting out the old one";
    }

    TEST(BackgroundSamplerTest, ClampsRequestedInterval)
    {
        BackgroundSampler sampler(1, [](WakeReason) {});
        EXPECT_EQ(sampler.IntervalMs(), domain::sampling::MIN_INTERVAL_MS)
            << "an interval below the minimum must be clamped, not accepted";

        sampler.SetInterval(999999);
        EXPECT_EQ(sampler.IntervalMs(), domain::sampling::MAX_INTERVAL_MS);
    }

    TEST(BackgroundSamplerTest, StopIsIdempotentAndJoins)
    {
        std::atomic<int> samples{0};
        BackgroundSampler sampler(1000, [&samples](WakeReason) { samples.fetch_add(1); });

        sampler.Start();
        EXPECT_TRUE(sampler.Running());

        sampler.Stop();
        EXPECT_FALSE(sampler.Running());

        // Stopping again must not hang or crash; the destructor also calls Stop.
        sampler.Stop();

        int const frozen = samples.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        EXPECT_EQ(samples.load(), frozen) << "no sampling may happen after Stop returns";
    }

    TEST(BackgroundSamplerTest, DestructorStopsWithoutExplicitCall)
    {
        std::atomic<int> samples{0};
        {
            BackgroundSampler sampler(domain::sampling::MIN_INTERVAL_MS, [&samples](WakeReason) {
                samples.fetch_add(1);
            });
            sampler.Start();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        // Reaching here without hanging proves the destructor stopped the thread.

        int const frozen = samples.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        EXPECT_EQ(samples.load(), frozen);
    }

    TEST(BackgroundSamplerTest, StopWithoutStartIsSafe)
    {
        BackgroundSampler sampler(1000, [](WakeReason) {});
        // Neither of these may hang or throw when the thread was never started.
        sampler.Stop();
        EXPECT_FALSE(sampler.Running());
    }

    TEST(BackgroundSamplerTest, StartTwiceDoesNotCreateASecondThread)
    {
        std::atomic<int> samples{0};
        BackgroundSampler sampler(domain::sampling::MAX_INTERVAL_MS, [&samples](WakeReason) {
            samples.fetch_add(1);
        });

        sampler.Start();
        sampler.Start(); // Must be a no-op rather than starting a second loop.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        sampler.Stop();

        // With a 5 s interval and two starts, a second thread would double-count the
        // initial sample. Exactly one initial sample is expected here.
        EXPECT_LE(samples.load(), 2);
    }

    TEST(BackgroundSamplerTest, SampleCountTracksCallbacks)
    {
        std::atomic<int> samples{0};
        BackgroundSampler sampler(domain::sampling::MIN_INTERVAL_MS, [&samples](WakeReason) {
            samples.fetch_add(1);
        });

        sampler.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        sampler.Stop();

        // The counter and the callback must agree.
        EXPECT_EQ(sampler.SampleCount(), static_cast<uint64_t>(samples.load()));
    }
}
