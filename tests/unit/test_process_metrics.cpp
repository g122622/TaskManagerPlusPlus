// clang-format off: Winsock 2 must precede windows.h.
#include <winsock2.h>
#include <windows.h>
// clang-format on
#include <gtest/gtest.h>

#include "Domain/ProcessModel.h"
#include "Domain/SystemModel.h"
#include "Platform/Windows/HardwareCounterProbe.h"
#include "Platform/Windows/ProcessNetworkProbe.h"
#include "Platform/Windows/WindowsProcessProbe.h"
#include "UI/Lists/ProcessListModel.h"

#include <chrono>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace tmpp::test {
namespace {
platform::ProcessSnapshot Snapshot(
    uint64_t seconds, uint64_t received, uint64_t sent, bool available = true, uint64_t creation = 123)
{
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    platform::ProcessSnapshot raw;
    raw.capturedAt = (seconds + 1) * frequency.QuadPart;
    platform::ProcessInfo process;
    process.identity = {42, creation};
    process.network = {received, sent, available};
    raw.processes.push_back(process);
    return raw;
}
} // namespace

TEST(ProcessMetricsTest, NetworkUsesTheSamplingIntervalAndKeepsBothDirections)
{
    domain::ProcessModel model(4);
    EXPECT_TRUE(model.Update(Snapshot(0, 100, 200), {}).processes[0].networkRatesUnavailable);
    auto const view = model.Update(Snapshot(2, 2100, 6200), {});
    EXPECT_FALSE(view.processes[0].networkRatesUnavailable);
    EXPECT_DOUBLE_EQ(view.processes[0].networkReceiveBytesPerSec, 1000.0);
    EXPECT_DOUBLE_EQ(view.processes[0].networkSendBytesPerSec, 3000.0);
}

TEST(ProcessMetricsTest, NetworkRecoversAfterMissingCountersAndRollbackWithoutSpikes)
{
    domain::ProcessModel model(4);
    model.Update(Snapshot(0, 100, 200), {});
    EXPECT_TRUE(model.Update(Snapshot(1, 0, 0, false), {}).processes[0].networkRatesUnavailable);
    EXPECT_TRUE(model.Update(Snapshot(2, 500, 600), {}).processes[0].networkRatesUnavailable);
    EXPECT_TRUE(model.Update(Snapshot(3, 10, 20), {}).processes[0].networkRatesUnavailable);
    auto const recovered = model.Update(Snapshot(4, 1010, 2020), {});
    EXPECT_FALSE(recovered.processes[0].networkRatesUnavailable);
    EXPECT_DOUBLE_EQ(recovered.processes[0].networkReceiveBytesPerSec, 1000.0);
    EXPECT_TRUE(model.Update(Snapshot(5, 500000, 500000, true, 456), {}).processes[0].networkRatesUnavailable);
}

TEST(ProcessMetricsTest, GpuMemoryIsPublishedAndMissingCountersStayUnavailable)
{
    domain::ProcessModel model(4);
    auto const view = model.Update(Snapshot(0, 0, 0), {}, {}, {{42, {1048576, 2097152, true}}});
    EXPECT_TRUE(view.processes[0].gpuMemory.available);
    EXPECT_EQ(view.processes[0].gpuMemory.dedicatedBytes, 1048576u);
    EXPECT_EQ(view.processes[0].gpuMemory.sharedBytes, 2097152u);
    EXPECT_FALSE(model.Update(Snapshot(1, 0, 0), {}).processes[0].gpuMemory.available);
}

TEST(ProcessMetricsTest, SortUsesTheDisplayedMetricAndPutsOnlyThatMetricsMissingValuesLast)
{
    domain::ProcessSnapshotView snapshot;
    snapshot.version = 1;
    snapshot.processes.resize(3);
    for (uint32_t i = 0; i < 3; ++i)
        snapshot.processes[i].identity.pid = i + 1;
    snapshot.processes[0].networkRatesUnavailable = false;
    snapshot.processes[0].networkSendBytesPerSec = 200;
    snapshot.processes[1].networkRatesUnavailable = false;
    snapshot.processes[1].networkReceiveBytesPerSec = 500;
    snapshot.processes[2].networkSendBytesPerSec = 9999; // Unavailable must still sort last.
    snapshot.processes[0].memory.workingSetSize = 5000;
    snapshot.processes[1].ratesUnavailable = false;
    snapshot.processes[1].memory.workingSetSize = 100;
    ui::ProcessListModel list;
    ui::ListQuery query;
    query.column = ui::SortColumn::Network;
    list.Update(snapshot, query);
    EXPECT_EQ(list.VisibleIndices(), (std::vector<uint32_t>{1, 0, 2}));
    query.direction = ui::SortDirection::Ascending;
    list.Update(snapshot, query);
    EXPECT_EQ(list.VisibleIndices(), (std::vector<uint32_t>{0, 1, 2}));
    query.column = ui::SortColumn::Memory;
    query.direction = ui::SortDirection::Descending;
    list.Update(snapshot, query);
    EXPECT_EQ(list.VisibleIndices()[0], 0u); // CPU's missing baseline must not affect memory sorting.
    snapshot.processes[0].basePriority = 8;
    snapshot.processes[1].basePriority = 13;
    snapshot.processes[2].basePriority = 4;
    ++snapshot.version;
    query.column = ui::SortColumn::Priority;
    list.Update(snapshot, query);
    EXPECT_EQ(list.VisibleIndices(), (std::vector<uint32_t>{1, 0, 2}));
}

TEST(ProcessNetworkIntegration, ReportsTrafficOrExplicitlyReportsUnavailable)
{
    platform::ProcessNetworkProbe probe;
    platform::WindowsProcessProbe processes;
    auto snapshot = processes.Enumerate();
    ASSERT_TRUE(snapshot.Success());
    probe.Read(snapshot.Value());
    if (!probe.Available()) {
        EXPECT_TRUE(probe.ErrorCode() == ERROR_ACCESS_DENIED || probe.ErrorCode() == ERROR_PRIVILEGE_NOT_HELD)
            << "Unexpected ETW setup failure: " << probe.ErrorCode();
        for (auto const& process : snapshot.Value().processes)
            EXPECT_FALSE(process.network.available);
        GTEST_SKIP() << "ETW unavailable, Windows error " << probe.ErrorCode()
                     << "; run elevated for traffic validation";
    }
    WSADATA data{};
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &data), 0);
    SOCKET receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_NE(receiver, INVALID_SOCKET);
    ASSERT_NE(sender, INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(receiver, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    int length = sizeof(address);
    ASSERT_EQ(getsockname(receiver, reinterpret_cast<sockaddr*>(&address), &length), 0);
    char buffer[1024]{};
    DWORD timeout = 1000;
    setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
    for (int i = 0; i < 8; ++i) {
        ASSERT_EQ(
            sendto(sender, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&address), length), sizeof(buffer));
        ASSERT_EQ(recv(receiver, buffer, sizeof(buffer), 0), sizeof(buffer));
    }
    closesocket(sender);
    closesocket(receiver);
    WSACleanup();
    uint64_t sent = 0;
    for (int i = 0; i < 30 && sent < sizeof(buffer) * 8; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        probe.Read(snapshot.Value());
        for (auto const& process : snapshot.Value().processes)
            if (process.identity.pid == GetCurrentProcessId()) sent = process.network.sentBytes;
    }
    EXPECT_GE(sent, sizeof(buffer) * 8u);
}
TEST(ProcessMetricsTest, KernelHistoryExcludesIdleAndStaysAlignedWhenAProbeFails)
{
    domain::SystemModel model(1, 1000, 60);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    model.Update({100, 200, 100}, {}, frequency.QuadPart, {{100, 200, 100}});
    model.Update({160, 300, 200}, {}, 2 * frequency.QuadPart, {{160, 300, 200}});
    auto history = model.History();
    EXPECT_DOUBLE_EQ(history.cpuTotal.back(), 70.0);
    EXPECT_DOUBLE_EQ(history.cpuKernel.back(), 20.0);
    ASSERT_EQ(history.perProcessorKernelCpu.size(), 1u);
    EXPECT_DOUBLE_EQ(history.perProcessorKernelCpu[0].back(), 20.0);
    model.Update({220, 400, 300}, {}, 3 * frequency.QuadPart, {});
    history = model.History();
    EXPECT_EQ(history.cpuKernel.size(), history.cpuTotal.size());
    EXPECT_EQ(history.perProcessorKernelCpu[0].size(), history.cpuTotal.size());
    EXPECT_DOUBLE_EQ(history.perProcessorKernelCpu[0].back(), 0.0);
}

TEST(ProcessGpuMemoryIntegration, ReadsTheSelectedAdaptersUsageInsteadOfCommitment)
{
    platform::HardwareCounterProbe probe;
    auto gpu = probe.ReadGpu();
    if (!gpu.Success() || !gpu.Value().available) GTEST_SKIP() << "GPU counters unavailable";
    auto snapshot = platform::WindowsProcessProbe{}.Enumerate();
    ASSERT_TRUE(snapshot.Success());
    std::map<uint32_t, platform::ProcessGpuMemory> memory;
    probe.ReadProcessGpuMemory(memory, snapshot.Value().processes);
    size_t populated = 0;
    uint64_t largest = 0;
    for (auto const& [pid, value] : memory) {
        if (!value.available || pid == 0) continue;
        ++populated;
        largest = (std::max)(largest, value.dedicatedBytes);
        if (gpu.Value().dedicatedTotalBytes > 0)
            EXPECT_LE(value.dedicatedBytes, gpu.Value().dedicatedTotalBytes) << "PID " << pid;
    }
    std::printf("GPU native process memory: %zu readable processes, largest %llu bytes, capacity %llu bytes\n",
        populated,
        static_cast<unsigned long long>(largest),
        static_cast<unsigned long long>(gpu.Value().dedicatedTotalBytes));
    if (populated == 0) GTEST_SKIP() << "No accessible GPU process segment groups";
}

} // namespace tmpp::test
