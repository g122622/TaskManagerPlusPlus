#include "Platform/Windows/ProcessNetworkProbe.h"

// clang-format off: Windows types must be declared before the ETW headers.
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <objbase.h>
// clang-format on

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace tmpp::platform {
namespace {
constexpr GUID TCP_GUID{0x9a280ac0, 0xc8e0, 0x11d1, {0x84, 0xe2, 0x00, 0xc0, 0x4f, 0xb9, 0x98, 0xa2}};
constexpr GUID UDP_GUID{0xbf3a50c5, 0xa9c9, 0x4988, {0xa0, 0x05, 0x2d, 0xf0, 0xb7, 0xc8, 0x0f, 0x80}};

void EnableProfilePrivilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return;
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    if (LookupPrivilegeValueW(nullptr, SE_SYSTEM_PROFILE_NAME, &privileges.Privileges[0].Luid)) {
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
    }
    CloseHandle(token);
}
} // namespace

struct ProcessNetworkProbe::State {
    struct Counters {
        ProcessNetworkCounters bytes;
        uint64_t createTime{0};
    };
    std::wstring name;
    std::vector<unsigned char> propertiesBuffer;
    TRACEHANDLE session{0};
    TRACEHANDLE consumer{INVALID_PROCESSTRACE_HANDLE};
    std::thread worker;
    std::mutex mutex;
    std::map<uint32_t, Counters> totals;
    std::atomic<bool> running{false};
    std::atomic<uint32_t> error{ERROR_SUCCESS};

    EVENT_TRACE_PROPERTIES* Properties() { return reinterpret_cast<EVENT_TRACE_PROPERTIES*>(propertiesBuffer.data()); }

    static void WINAPI OnEvent(EVENT_RECORD* event)
    {
        bool const tcp = std::memcmp(&event->EventHeader.ProviderId, &TCP_GUID, sizeof(GUID)) == 0;
        bool const udp = std::memcmp(&event->EventHeader.ProviderId, &UDP_GUID, sizeof(GUID)) == 0;
        if ((!tcp && !udp) || event->EventHeader.EventDescriptor.Version < 1 || event->UserDataLength < 8) return;

        // Both TCP and UDP use Send/Recv (10/11) and SendIPV6/RecvIPV6 (26/27).
        // Read the owning PID from the payload: the header can name the system receive thread.
        auto const opcode = event->EventHeader.EventDescriptor.Opcode;
        bool const send = opcode == 10 || opcode == 26;
        bool const receive = opcode == 11 || opcode == 27;
        if (!send && !receive) return;
        uint32_t payload[2]{};
        std::memcpy(payload, event->UserData, sizeof(payload));
        auto& self = *static_cast<State*>(event->UserContext);
        std::lock_guard const lock(self.mutex);
        // Read() registers only live PIDs. This also bounds storage during process churn.
        auto const found = self.totals.find(payload[0]);
        if (found == self.totals.end() ||
            static_cast<uint64_t>(event->EventHeader.TimeStamp.QuadPart) < found->second.createTime)
            return;
        if (send)
            found->second.bytes.sentBytes += payload[1];
        else
            found->second.bytes.receivedBytes += payload[1];
    }
};

ProcessNetworkProbe::ProcessNetworkProbe()
    : m_state(std::make_unique<State>())
{
    auto& state = *m_state;
    EnableProfilePrivilege();
    static std::atomic<uint32_t> sequence{0};
    state.name = L"TaskManagerPlusPlus.Network." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(++sequence);
    state.propertiesBuffer.resize(sizeof(EVENT_TRACE_PROPERTIES) + (state.name.size() + 1) * sizeof(wchar_t));
    auto* properties = state.Properties();
    properties->Wnode.BufferSize = static_cast<ULONG>(state.propertiesBuffer.size());
    properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    // A custom system logger must have its own GUID; SystemTraceControlGuid is reserved
    // for the shared NT Kernel Logger and produces ERROR_INVALID_PARAMETER here.
    if (FAILED(CoCreateGuid(&properties->Wnode.Guid))) {
        state.error = ERROR_GEN_FAILURE;
        return;
    }
    properties->Wnode.ClientContext = 1;
    properties->BufferSize = 64;
    properties->MinimumBuffers = 4;
    properties->MaximumBuffers = 64;
    properties->FlushTimer = 1;
    properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;
    properties->EnableFlags = EVENT_TRACE_FLAG_NETWORK_TCPIP;
    properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    auto const started = StartTraceW(&state.session, state.name.c_str(), properties);
    state.error = started;
    if (started != ERROR_SUCCESS) return;

    EVENT_TRACE_LOGFILEW log{};
    log.LoggerName = state.name.data();
    log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    log.EventRecordCallback = State::OnEvent;
    log.Context = &state;
    state.consumer = OpenTraceW(&log);
    if (state.consumer == INVALID_PROCESSTRACE_HANDLE) {
        state.error = GetLastError();
        ControlTraceW(state.session, state.name.c_str(), properties, EVENT_TRACE_CONTROL_STOP);
        state.session = 0;
        return;
    }
    state.running = true;
    state.worker = std::thread([&state] {
        auto const result = ProcessTrace(&state.consumer, 1, nullptr, nullptr);
        state.error = result;
        state.running = false;
    });
}

ProcessNetworkProbe::~ProcessNetworkProbe()
{
    auto& state = *m_state;
    if (state.session != 0)
        ControlTraceW(state.session, state.name.c_str(), state.Properties(), EVENT_TRACE_CONTROL_STOP);
    if (state.consumer != INVALID_PROCESSTRACE_HANDLE) CloseTrace(state.consumer);
    if (state.worker.joinable()) state.worker.join();
}

bool ProcessNetworkProbe::Available() const noexcept
{
    return m_state->running.load();
}
uint32_t ProcessNetworkProbe::ErrorCode() const noexcept
{
    return m_state->error.load();
}

void ProcessNetworkProbe::Read(ProcessSnapshot& snapshot)
{
    auto& state = *m_state;
    std::lock_guard const lock(state.mutex);
    std::map<uint32_t, State::Counters> live;
    for (auto& process : snapshot.processes) {
        auto const found = state.totals.find(process.identity.pid);
        State::Counters counters;
        if (found != state.totals.end() && found->second.createTime == process.identity.createTime)
            counters = found->second;
        counters.createTime = process.identity.createTime;
        counters.bytes.available = state.running;
        process.network = counters.bytes;
        live.emplace(process.identity.pid, counters);
    }
    state.totals = std::move(live);
}
} // namespace tmpp::platform
