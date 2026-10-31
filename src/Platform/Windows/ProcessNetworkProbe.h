#pragma once

#include "Platform/ProcessTypes.h"
#include <memory>

namespace tmpp::platform {
// Owns an independent ETW session. Never attaches to or stops another application's logger.
class ProcessNetworkProbe {
public:
    ProcessNetworkProbe();
    ~ProcessNetworkProbe();
    ProcessNetworkProbe(ProcessNetworkProbe const&) = delete;
    ProcessNetworkProbe& operator=(ProcessNetworkProbe const&) = delete;

    void Read(ProcessSnapshot& snapshot);
    [[nodiscard]] bool Available() const noexcept;
    [[nodiscard]] uint32_t ErrorCode() const noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};
} // namespace tmpp::platform
