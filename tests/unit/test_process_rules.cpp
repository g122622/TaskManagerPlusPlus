// Tests for the saved process rules: what they match, when they fire, and how they survive a file.
//
// A rule is the only part of the tuning sidebar that acts without being asked. It applies itself to a
// process the user has not selected, some time after they created it, and possibly to several
// instances at once -- so the properties that matter are not "does it apply the value" but:
//
//   * it fires once per process, not once per sample;
//   * it fires again for a process that has restarted, which is the whole reason it exists;
//   * it never fires for a process the user did not name;
//   * a disabled rule fires for nothing;
//   * what it did is reported and then forgotten, so the same success is not announced every second.
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "Core/ProcessRules.h"
#include "Platform/FileSystem.h"

namespace tmpp::core::test
{
    namespace
    {
        [[nodiscard]] domain::ProcessView _process(uint32_t pid,
                                                   uint64_t createTime,
                                                   std::string imageName,
                                                   uint32_t parentPid = 0)
        {
            domain::ProcessView view;
            view.identity.pid = pid;
            view.identity.createTime = createTime;
            view.imageName = std::move(imageName);
            view.parentPid = parentPid;
            return view;
        }

        [[nodiscard]] domain::ProcessSnapshotView _snapshot(std::vector<domain::ProcessView> processes)
        {
            domain::ProcessSnapshotView snapshot;
            snapshot.processes = std::move(processes);
            snapshot.version = 1;
            return snapshot;
        }

        [[nodiscard]] ProcessRule _rule(std::string executable, platform::ProcessTuningChange settings, bool enabled = true)
        {
            ProcessRule rule;
            rule.executableName = std::move(executable);
            rule.settings = std::move(settings);
            rule.enabled = enabled;
            return rule;
        }

        /// A recording applier, so a test can assert on what was asked for rather than on a result.
        struct RecordingApplier
        {
            struct Call
            {
                uint32_t pid{0};
                uint64_t createTime{0};
                platform::ProcessTuningChange change;
            };

            std::vector<Call> calls;

            /// Outcome to report. Succeeded by default.
            platform::ProcessActionOutcome outcome{platform::ProcessActionOutcome::Succeeded};
            uint32_t applied{1};

            [[nodiscard]] ProcessRuleEngine::Applier Handler()
            {
                return [this](uint32_t pid, uint64_t createTime, platform::ProcessTuningChange const& change) {
                    calls.push_back(Call{pid, createTime, change});

                    platform::ProcessActionResult result;
                    result.outcome = outcome;
                    result.requested = change.Count();
                    result.applied = (outcome == platform::ProcessActionOutcome::Succeeded) ? applied : 0;
                    return result;
                };
            }
        };

        [[nodiscard]] std::string _temporaryPath()
        {
            // Beside the test executable rather than in the user's configuration directory: a test that
            // wrote to %LOCALAPPDATA% would overwrite the rules of whoever ran it.
            return "tmpp_test_process_rules.json";
        }
    }

    // ========================================================================
    // The change itself
    // ========================================================================

    TEST(ProcessTuningChangeTest, AnEmptyChangeCountsAsNothing)
    {
        platform::ProcessTuningChange change;
        EXPECT_TRUE(change.Empty());
        EXPECT_EQ(change.Count(), 0u);
    }

    TEST(ProcessTuningChangeTest, EachFieldIsCountedOnce)
    {
        platform::ProcessTuningChange change;
        change.cpuPriority = platform::ProcessPriorityClass::High;
        change.efficiencyMode = platform::ProcessEfficiencyMode::Off;
        change.affinityMask = 0xF;

        EXPECT_FALSE(change.Empty());
        EXPECT_EQ(change.Count(), 3u);
    }

    TEST(ProcessTuningStateTest, AnUnrestrictedAffinityIsRecognised)
    {
        // The distinction a rule depends on: a process using every processor has no restriction worth
        // recording, while a process using all but one does.
        platform::ProcessTuningState state;
        state.affinityAvailable = true;
        state.affinityGroupSize = 8;

        state.affinityMask = 0xFF;
        EXPECT_TRUE(state.AffinityIsUnrestricted());

        state.affinityMask = 0xFE;
        EXPECT_FALSE(state.AffinityIsUnrestricted());
    }

    TEST(ProcessTuningStateTest, AFullGroupIsUnrestricted)
    {
        // A group of exactly 64 is the widest the legacy affinity API addresses, and its full mask is
        // every bit set. This case is called out because the general expression for a full mask shifts
        // by the group size, and shifting a 64-bit value by 64 is undefined behaviour -- it produced a
        // different answer in debug and release builds, which is what made a rule for an unrestricted
        // process record an affinity restriction that was not there.
        platform::ProcessTuningState state;
        state.affinityAvailable = true;
        state.affinityGroupSize = 64;

        state.affinityMask = ~uint64_t{0};
        EXPECT_TRUE(state.AffinityIsUnrestricted());

        // One processor withheld is a restriction, and it is the one a rule has to record.
        state.affinityMask = ~uint64_t{0} & ~uint64_t{1};
        EXPECT_FALSE(state.AffinityIsUnrestricted());
    }

    TEST(ProcessTuningStateTest, AGroupLargerThanTheMaskIsNeverCalledUnrestricted)
    {
        // Not something Windows produces -- a processor group holds at most 64 -- so this only guards
        // the expression against a nonsense value read from somewhere. Reporting "restricted" would be a
        // wrong answer; false is the answer that records nothing.
        platform::ProcessTuningState state;
        state.affinityAvailable = true;
        state.affinityGroupSize = 128;
        state.affinityMask = ~uint64_t{0};

        EXPECT_FALSE(state.AffinityIsUnrestricted());
    }

    // ========================================================================
    // Matching
    // ========================================================================

    TEST(ProcessRuleEngineTest, ARuleFiresForTheProcessItNames)
    {
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::High;
        engine.SetRules({_rule("game.exe", settings)});

        RecordingApplier recorder;
        engine.Evaluate(_snapshot({_process(100, 1, "game.exe"), _process(200, 1, "notepad.exe")}),
                        recorder.Handler());

        ASSERT_EQ(recorder.calls.size(), 1u);
        EXPECT_EQ(recorder.calls[0].pid, 100u);
        EXPECT_TRUE(recorder.calls[0].change.cpuPriority.has_value());
    }

    TEST(ProcessRuleEngineTest, TheImageNameIsMatchedWithoutRegardToCase)
    {
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.efficiencyMode = platform::ProcessEfficiencyMode::Off;
        engine.SetRules({_rule("Game.EXE", settings)});

        RecordingApplier recorder;
        engine.Evaluate(_snapshot({_process(1, 1, "gAmE.exe")}), recorder.Handler());

        EXPECT_EQ(recorder.calls.size(), 1u);
    }

    TEST(ProcessRuleEngineTest, ARuleFiresOncePerProcessAndNotOncePerSample)
    {
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::BelowNormal;
        engine.SetRules({_rule("build.exe", settings)});

        RecordingApplier recorder;
        domain::ProcessSnapshotView const sample = _snapshot({_process(42, 7, "build.exe")});

        engine.Evaluate(sample, recorder.Handler());
        engine.Evaluate(sample, recorder.Handler());
        engine.Evaluate(sample, recorder.Handler());

        EXPECT_EQ(recorder.calls.size(), 1u);
    }

    TEST(ProcessRuleEngineTest, ARestartedProcessIsTreatedAsNew)
    {
        // The point of the feature: a program that is closed and opened again comes back with the
        // operating system's defaults, and the rule is what puts them back.
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.ioPriority = platform::ProcessIoPriority::Low;
        engine.SetRules({_rule("indexer.exe", settings)});

        RecordingApplier recorder;

        // Same identifier, different creation time: Windows has handed the number to a new process.
        engine.Evaluate(_snapshot({_process(500, 1000, "indexer.exe")}), recorder.Handler());
        engine.Evaluate(_snapshot({_process(500, 2000, "indexer.exe")}), recorder.Handler());

        EXPECT_EQ(recorder.calls.size(), 2u);
        EXPECT_NE(recorder.calls[0].createTime, recorder.calls[1].createTime);
    }

    TEST(ProcessRuleEngineTest, ADisabledRuleFiresForNothing)
    {
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::High;
        engine.SetRules({_rule("game.exe", settings, /*enabled=*/false)});

        EXPECT_FALSE(engine.HasActiveRules());

        RecordingApplier recorder;
        engine.Evaluate(_snapshot({_process(1, 1, "game.exe")}), recorder.Handler());

        EXPECT_TRUE(recorder.calls.empty());
    }

    TEST(ProcessRuleEngineTest, OnlyTheFirstMatchingRuleIsApplied)
    {
        // Two rules for one name would otherwise both fire in the same round and the second would
        // silently win, making the outcome depend on the order they were created in.
        ProcessRuleEngine engine;

        platform::ProcessTuningChange first;
        first.cpuPriority = platform::ProcessPriorityClass::High;

        platform::ProcessTuningChange second;
        second.cpuPriority = platform::ProcessPriorityClass::Idle;

        engine.SetRules({_rule("game.exe", first), _rule("game.exe", second)});

        RecordingApplier recorder;
        engine.Evaluate(_snapshot({_process(1, 1, "game.exe")}), recorder.Handler());

        ASSERT_EQ(recorder.calls.size(), 1u);
        ASSERT_TRUE(recorder.calls[0].change.cpuPriority.has_value());
        EXPECT_EQ(recorder.calls[0].change.cpuPriority.value(), platform::ProcessPriorityClass::High);
    }

    TEST(ProcessRuleEngineTest, AProcessWithNoImageNameIsNotMatched)
    {
        // The idle process and the memory compression store have no executable behind them, so no rule
        // can name them and an empty name must not match an empty rule.
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::High;
        engine.SetRules({_rule("game.exe", settings)});

        RecordingApplier recorder;
        engine.Evaluate(_snapshot({_process(0, 1, "")}), recorder.Handler());

        EXPECT_TRUE(recorder.calls.empty());
    }

    TEST(ProcessRuleEngineTest, ChangingTheRulesLetsThemApplyAgain)
    {
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::High;
        engine.SetRules({_rule("game.exe", settings)});

        RecordingApplier recorder;
        domain::ProcessSnapshotView const sample = _snapshot({_process(9, 3, "game.exe")});

        engine.Evaluate(sample, recorder.Handler());
        ASSERT_EQ(recorder.calls.size(), 1u);

        // Editing a rule must take effect on the next sample rather than only on the next launch of the
        // program it names, or the edit would appear to do nothing.
        settings.cpuPriority = platform::ProcessPriorityClass::Idle;
        engine.SetRules({_rule("game.exe", settings)});
        engine.Evaluate(sample, recorder.Handler());

        ASSERT_EQ(recorder.calls.size(), 2u);
        ASSERT_TRUE(recorder.calls[1].change.cpuPriority.has_value());
        EXPECT_EQ(recorder.calls[1].change.cpuPriority.value(), platform::ProcessPriorityClass::Idle);
    }

    // ========================================================================
    // Reporting
    // ========================================================================

    TEST(ProcessRuleEngineTest, ASuccessIsReported)
    {
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::High;
        engine.SetRules({_rule("game.exe", settings)});

        RecordingApplier recorder;
        engine.Evaluate(_snapshot({_process(1, 1, "game.exe")}), recorder.Handler());

        std::vector<ProcessRuleEvent> const events = engine.TakeEvents();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_FALSE(events[0].Failed());
        EXPECT_EQ(events[0].executableName, "game.exe");
        EXPECT_EQ(engine.AppliedCount(), 1u);
    }

    TEST(ProcessRuleEngineTest, AProcessThatHasAlreadyExitedIsNotReported)
    {
        // A rule fires the moment a process is first observed, and a short-lived one can be gone by the
        // time the change is attempted. That is the ordinary race rather than news.
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::High;
        engine.SetRules({_rule("flash.exe", settings)});

        RecordingApplier recorder;
        recorder.outcome = platform::ProcessActionOutcome::NotFound;
        engine.Evaluate(_snapshot({_process(1, 1, "flash.exe")}), recorder.Handler());

        EXPECT_TRUE(engine.TakeEvents().empty());
        EXPECT_EQ(engine.AppliedCount(), 0u);
    }

    TEST(ProcessRuleEngineTest, ARefusalIsReported)
    {
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::High;
        engine.SetRules({_rule("protected.exe", settings)});

        RecordingApplier recorder;
        recorder.outcome = platform::ProcessActionOutcome::AccessDenied;
        engine.Evaluate(_snapshot({_process(1, 1, "protected.exe")}), recorder.Handler());

        std::vector<ProcessRuleEvent> const events = engine.TakeEvents();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_TRUE(events[0].Failed());
    }

    TEST(ProcessRuleEngineTest, TakingTheEventsEmptiesTheQueue)
    {
        ProcessRuleEngine engine;

        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::High;
        engine.SetRules({_rule("game.exe", settings)});

        RecordingApplier recorder;
        engine.Evaluate(_snapshot({_process(1, 1, "game.exe")}), recorder.Handler());

        EXPECT_EQ(engine.TakeEvents().size(), 1u);
        EXPECT_TRUE(engine.TakeEvents().empty());
    }

    // ========================================================================
    // The file
    // ========================================================================

    TEST(ProcessRuleStoreTest, RulesSurviveARoundTrip)
    {
        std::string const path = _temporaryPath();
        std::remove(path.c_str());

        ProcessRuleStore store{path};

        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::AboveNormal;
        settings.memoryPriority = platform::ProcessMemoryPriority::Low;
        settings.ioPriority = platform::ProcessIoPriority::High;
        settings.efficiencyMode = platform::ProcessEfficiencyMode::Off;
        settings.affinityMask = 0x0F;

        std::vector<ProcessRule> const written{_rule("game.exe", settings),
                                               _rule("build.exe", platform::ProcessTuningChange{}, /*enabled=*/false)};

        ASSERT_TRUE(store.Save(written).Success());

        std::vector<ProcessRule> const loaded = store.Load();

        // The second rule asks for nothing, so it is dropped: it could never do anything, and keeping it
        // would make the matching loop check for a case that cannot occur.
        ASSERT_EQ(loaded.size(), 1u);

        EXPECT_EQ(loaded[0].executableName, "game.exe");
        ASSERT_TRUE(loaded[0].settings.cpuPriority.has_value());
        EXPECT_EQ(loaded[0].settings.cpuPriority.value(), platform::ProcessPriorityClass::AboveNormal);
        ASSERT_TRUE(loaded[0].settings.memoryPriority.has_value());
        EXPECT_EQ(loaded[0].settings.memoryPriority.value(), platform::ProcessMemoryPriority::Low);
        ASSERT_TRUE(loaded[0].settings.ioPriority.has_value());
        EXPECT_EQ(loaded[0].settings.ioPriority.value(), platform::ProcessIoPriority::High);
        ASSERT_TRUE(loaded[0].settings.efficiencyMode.has_value());
        EXPECT_EQ(loaded[0].settings.efficiencyMode.value(), platform::ProcessEfficiencyMode::Off);
        ASSERT_TRUE(loaded[0].settings.affinityMask.has_value());
        EXPECT_EQ(loaded[0].settings.affinityMask.value(), 0x0Fu);

        std::remove(path.c_str());
    }

    TEST(ProcessRuleStoreTest, AMissingFileIsNoRulesRatherThanAnError)
    {
        // Rules are an accelerator. A machine that has never used them must not log a failure.
        ProcessRuleStore store{"tmpp_a_file_that_does_not_exist.json"};
        EXPECT_TRUE(store.Load().empty());
    }

    TEST(ProcessRuleStoreTest, AHandEditedFileWithNonsenseYieldsWhatCanBeRead)
    {
        std::string const path = _temporaryPath();
        std::remove(path.c_str());

        // Written by hand, as the file is meant to be: one usable rule, one with an unrecognised value,
        // one naming no executable, and one that is not an object at all.
        std::string const contents = R"({
          "version": 1,
          "rules": [
            { "executable": "good.exe", "enabled": true, "settings": { "cpuPriority": "high" } },
            { "executable": "odd.exe", "enabled": true, "settings": { "cpuPriority": "ludicrous" } },
            { "enabled": true, "settings": { "efficiencyMode": "on" } },
            17
          ]
        })";

        ASSERT_TRUE(platform::AtomicWriteFile(path, contents).Success());

        ProcessRuleStore store{path};
        std::vector<ProcessRule> const loaded = store.Load();

        // "odd.exe" is kept but its unrecognised value is dropped, so the rule asks for nothing and is
        // discarded. Nothing is guessed at.
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_EQ(loaded[0].executableName, "good.exe");
        ASSERT_TRUE(loaded[0].settings.cpuPriority.has_value());
        EXPECT_EQ(loaded[0].settings.cpuPriority.value(), platform::ProcessPriorityClass::High);

        std::remove(path.c_str());
    }

    TEST(ProcessRuleStoreTest, AFileThatIsNotJsonIsNoRulesRatherThanAnError)
    {
        std::string const path = _temporaryPath();
        std::remove(path.c_str());

        ASSERT_TRUE(platform::AtomicWriteFile(path, "this is not json at all").Success());

        ProcessRuleStore store{path};
        EXPECT_TRUE(store.Load().empty());

        std::remove(path.c_str());
    }

    // ========================================================================
    // Description
    // ========================================================================

    TEST(ProcessRuleTest, TheDescriptionNamesEverySettingTheRuleCarries)
    {
        platform::ProcessTuningChange settings;
        settings.cpuPriority = platform::ProcessPriorityClass::High;
        settings.efficiencyMode = platform::ProcessEfficiencyMode::On;
        settings.affinityMask = 0xFF;

        ProcessRule const rule = _rule("game.exe", settings);
        std::string const description = rule.Describe();

        EXPECT_NE(description.find("High"), std::string::npos);
        EXPECT_NE(description.find("Efficiency"), std::string::npos);
        EXPECT_NE(description.find("FF"), std::string::npos);
    }

    TEST(ProcessRuleTest, ARuleThatAsksForNothingIsNotValid)
    {
        EXPECT_FALSE(_rule("game.exe", platform::ProcessTuningChange{}).Valid());
        EXPECT_FALSE(_rule("", platform::ProcessTuningChange{}).Valid());
    }
}
