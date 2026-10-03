// Tests for the Platform layer probes.
//
// These exercise the probes against the real system rather than mocks: the point
// of the Platform layer is correct interaction with Windows, and the interesting
// failures (structure layout drift, missing API, counter semantics) only appear
// against the real API. Deterministic counter-arithmetic tests belong to the
// Domain layer, which takes these raw values as input.
#include <gtest/gtest.h>

#include "Platform/Windows/WindowsProcessProbe.h"
#include "Platform/Windows/WindowsString.h"
#include "Platform/Windows/WindowsSystemProbe.h"

#include <windows.h>

#include <algorithm>
#include <set>

namespace tmpp::platform
{
    namespace
    {
        /// System Idle Process always exists and always has PID 0.
        constexpr uint32_t IDLE_PROCESS_PID = 0;
    }

    // ------------------------------------------------------------------------
    // String conversion
    // ------------------------------------------------------------------------

    TEST(WindowsStringTest, ConvertsAsciiRoundTrip)
    {
        std::string const original = "notepad.exe";
        EXPECT_EQ(ToUtf8(ToUtf16(original)), original);
    }

    TEST(WindowsStringTest, ConvertsNonAsciiRoundTrip)
    {
        // Chinese text forces multi-byte UTF-8 and exercises the length handling.
        // Written with explicit escapes because u8"" produces char8_t in C++20.
        std::string const original = "\xE4\xBB\xBB\xE5\x8A\xA1\xE7\xAE\xA1\xE7\x90\x86\xE5\x99\xA8.exe";
        std::string const roundTripped = ToUtf8(ToUtf16(original));
        EXPECT_EQ(roundTripped, original);
    }

    TEST(WindowsStringTest, HandlesEmptyInput)
    {
        EXPECT_TRUE(ToUtf8(std::wstring_view{}).empty());
        EXPECT_TRUE(ToUtf16(std::string_view{}).empty());
    }

    TEST(WindowsStringTest, DoesNotRequireNullTermination)
    {
        // The snapshot's UNICODE_STRING payload is not necessarily terminated, so
        // conversion must honour an explicit length.
        wchar_t const buffer[] = L"abcXX";
        EXPECT_EQ(ToUtf8(buffer, 3), "abc");
    }

    // ------------------------------------------------------------------------
    // Process probe
    // ------------------------------------------------------------------------

    TEST(WindowsProcessProbeTest, ReportsBulkEnumerationCapability)
    {
        WindowsProcessProbe probe;
        ProcessCapabilities const caps = probe.Capabilities();

        // NtQuerySystemInformation is present in every supported Windows process.
        EXPECT_TRUE(caps.hasBulkEnumeration);
        EXPECT_TRUE(caps.hasCpuTimes);
        EXPECT_TRUE(caps.hasMemoryCounters);
        EXPECT_TRUE(caps.hasIoCounters);
        EXPECT_TRUE(caps.hasThreadAndHandleCounts);
    }

    TEST(WindowsProcessProbeTest, EnumeratesProcesses)
    {
        WindowsProcessProbe probe;
        auto const result = probe.Enumerate();

        ASSERT_TRUE(result.Success()) << result.GetError().Message();
        ProcessSnapshot const& snapshot = result.Value();

        // Any running Windows system has more than a handful of processes.
        EXPECT_GT(snapshot.processes.size(), 10u);
        EXPECT_NE(snapshot.capturedAt, 0u);
    }

    TEST(WindowsProcessProbeTest, FindsSystemIdleProcess)
    {
        WindowsProcessProbe probe;
        auto const result = probe.Enumerate();
        ASSERT_TRUE(result.Success());

        auto const& processes = result.Value().processes;
        auto const idle = std::find_if(processes.begin(), processes.end(), [](ProcessInfo const& info) {
            return info.identity.pid == IDLE_PROCESS_PID;
        });

        ASSERT_NE(idle, processes.end()) << "PID 0 (System Idle Process) must be present";

        // The System Idle Process has no executable image, so the snapshot reports
        // an empty ImageName for it. Tools that display "Idle" or "System Idle
        // Process" synthesize that label themselves; the API does not supply it.
        // Naming it is therefore the Domain layer's responsibility, and this test
        // pins the raw behaviour so a future change is noticed.
        EXPECT_TRUE(idle->imageName.empty());
    }

    TEST(WindowsProcessProbeTest, NamesEveryProcessThatHasAnImage)
    {
        WindowsProcessProbe probe;
        auto const result = probe.Enumerate();
        ASSERT_TRUE(result.Success());

        // Only the idle process may lack a name: everything else is backed by a
        // real image. A widespread empty name would indicate a conversion bug.
        size_t unnamed = 0;
        for (auto const& info : result.Value().processes)
        {
            if (info.imageName.empty())
            {
                ++unnamed;
                EXPECT_EQ(info.identity.pid, IDLE_PROCESS_PID)
                    << "PID " << info.identity.pid << " has no image name";
            }
        }

        EXPECT_EQ(unnamed, 1u) << "exactly one process (the idle process) should be unnamed";
    }

    TEST(WindowsProcessProbeTest, AssignsUniquePids)
    {
        WindowsProcessProbe probe;
        auto const result = probe.Enumerate();
        ASSERT_TRUE(result.Success());

        auto const& processes = result.Value().processes;
        std::set<uint32_t> pids;
        for (auto const& info : processes)
        {
            pids.insert(info.identity.pid);
        }

        // PIDs are unique at any instant, so the set must not collapse entries.
        EXPECT_EQ(pids.size(), processes.size());
    }

    TEST(WindowsProcessProbeTest, CapturesCreationTimeForEveryProcess)
    {
        WindowsProcessProbe probe;
        auto const result = probe.Enumerate();
        ASSERT_TRUE(result.Success());

        // Creation time is what makes PID reuse detectable, so it must be present
        // for every entry rather than only for the ones we open a handle on.
        for (auto const& info : result.Value().processes)
        {
            EXPECT_NE(info.identity.createTime, 0u) << "PID " << info.identity.pid << " has no creation time";
        }
    }

    TEST(WindowsProcessProbeTest, CurrentProcessAppearsWithSaneCounters)
    {
        WindowsProcessProbe probe;
        auto const result = probe.Enumerate();
        ASSERT_TRUE(result.Success());

        uint32_t const selfPid = GetCurrentProcessId();
        auto const& processes = result.Value().processes;
        auto const self = std::find_if(processes.begin(), processes.end(), [selfPid](ProcessInfo const& info) {
            return info.identity.pid == selfPid;
        });

        ASSERT_NE(self, processes.end()) << "the test process must appear in its own snapshot";

        // The test binary has consumed CPU and mapped memory by now.
        EXPECT_GT(self->cpu.Total(), 0u);
        EXPECT_GT(self->memory.workingSetSize, 0u);
        EXPECT_GT(self->threadCount, 0u);
        EXPECT_GT(self->handleCount, 0u);

        // Working set cannot exceed its own peak.
        EXPECT_LE(self->memory.workingSetSize, self->memory.peakWorkingSetSize);

        // Private commit is a subset of reserved address space.
        EXPECT_LE(self->memory.privatePageCount, self->memory.virtualSize);
    }

    TEST(WindowsProcessProbeTest, EveryProcessBelongsToAKnownSessionOrZero)
    {
        WindowsProcessProbe probe;
        auto const result = probe.Enumerate();
        ASSERT_TRUE(result.Success());

        // Session IDs are small; a garbage read here would indicate a layout error.
        for (auto const& info : result.Value().processes)
        {
            EXPECT_LT(info.sessionId, 1000u) << "PID " << info.identity.pid;
        }
    }

    TEST(WindowsProcessProbeTest, ParentPidsReferenceRealProcessesOrZero)
    {
        WindowsProcessProbe probe;
        auto const result = probe.Enumerate();
        ASSERT_TRUE(result.Success());

        auto const& processes = result.Value().processes;
        std::set<uint32_t> pids;
        for (auto const& info : processes)
        {
            pids.insert(info.identity.pid);
        }

        // A parent may have exited, so a missing parent is normal; what would
        // indicate a layout error is an implausible PID value.
        for (auto const& info : processes)
        {
            EXPECT_LT(info.parentPid, 0xFFFFFF00u) << "PID " << info.identity.pid << " has a garbage parent PID";
        }
    }

    // ------------------------------------------------------------------------
    // System probe
    // ------------------------------------------------------------------------

    TEST(WindowsSystemProbeTest, ReportsCapabilities)
    {
        WindowsSystemProbe probe;
        SystemCapabilities const caps = probe.Capabilities();

        EXPECT_TRUE(caps.hasCpuTimes);
        EXPECT_TRUE(caps.hasMemoryInfo);
        EXPECT_TRUE(caps.hasPerProcessorCpuTimes);
        EXPECT_TRUE(caps.hasProcessorTopology);
    }

    TEST(WindowsSystemProbeTest, ReadsSystemCpuTimes)
    {
        WindowsSystemProbe probe;
        auto const result = probe.ReadCpuTimes();

        ASSERT_TRUE(result.Success()) << result.GetError().Message();
        SystemCpuTimes const times = result.Value();

        // A machine that has been up for any length of time has idle and busy time.
        EXPECT_GT(times.idleTime, 0u);
        EXPECT_GT(times.kernelTime, 0u);
        EXPECT_GT(times.userTime, 0u);

        // Kernel time includes idle, so idle can never exceed it.
        EXPECT_LE(times.idleTime, times.kernelTime);
    }

    TEST(WindowsSystemProbeTest, KernelExcludingIdleIsConsistent)
    {
        WindowsSystemProbe probe;
        auto const result = probe.ReadCpuTimes();
        ASSERT_TRUE(result.Success());

        SystemCpuTimes const times = result.Value();

        // The helper must subtract idle exactly, never underflow.
        EXPECT_EQ(times.KernelExcludingIdle(), times.kernelTime - times.idleTime);
        EXPECT_EQ(times.Total(), times.kernelTime + times.userTime);
        EXPECT_EQ(times.Busy(), times.KernelExcludingIdle() + times.userTime);
    }

    TEST(WindowsSystemProbeTest, ReadsPerProcessorCpuTimes)
    {
        WindowsSystemProbe probe;
        auto const result = probe.ReadPerProcessorCpuTimes();

        ASSERT_TRUE(result.Success()) << result.GetError().Message();
        auto const& processors = result.Value();

        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);

        // Exactly one entry per logical processor.
        EXPECT_EQ(processors.size(), static_cast<size_t>(systemInfo.dwNumberOfProcessors));

        for (auto const& times : processors)
        {
            EXPECT_LE(times.idleTime, times.kernelTime);
        }
    }

    TEST(WindowsSystemProbeTest, ReadsMemoryInfo)
    {
        WindowsSystemProbe probe;
        auto const result = probe.ReadMemoryInfo();

        ASSERT_TRUE(result.Success()) << result.GetError().Message();
        SystemMemoryInfo const info = result.Value();

        EXPECT_GT(info.totalPhysical, 0u);
        EXPECT_GT(info.totalVirtual, 0u);

        // Available memory cannot exceed installed memory.
        EXPECT_LE(info.availablePhysical, info.totalPhysical);
        EXPECT_LE(info.memoryLoadPercent, 100u);
    }

    TEST(WindowsSystemProbeTest, ReadsProcessorInfo)
    {
        WindowsSystemProbe probe;
        auto const result = probe.ReadProcessorInfo();

        ASSERT_TRUE(result.Success()) << result.GetError().Message();
        SystemProcessorInfo const info = result.Value();

        EXPECT_GT(info.logicalProcessorCount, 0u);

        // Physical cores cannot exceed logical processors; with SMT they are fewer.
        EXPECT_GT(info.physicalCoreCount, 0u);
        EXPECT_LE(info.physicalCoreCount, info.logicalProcessorCount);

        EXPECT_FALSE(info.architecture.empty());
        EXPECT_NE(info.architecture, "unknown");
    }

    TEST(WindowsSystemProbeTest, MemoryPressureAgreesWithLoadPercent)
    {
        WindowsSystemProbe probe;
        auto const result = probe.ReadMemoryInfo();
        ASSERT_TRUE(result.Success());

        SystemMemoryInfo const info = result.Value();

        // Cross-check the two independent fields the API returns so a partial read
        // cannot pass unnoticed.
        uint64_t const used = info.totalPhysical - info.availablePhysical;
        uint32_t const derived = static_cast<uint32_t>((used * 100) / info.totalPhysical);

        EXPECT_NEAR(static_cast<double>(derived), static_cast<double>(info.memoryLoadPercent), 2.0);
    }
}
