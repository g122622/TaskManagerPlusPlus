// Temporary diagnostic: prints the extended system probe readings.
//
// Not part of the suite. It exists to confirm what this machine actually reports
// before the performance page is built around those fields, because a field that
// silently comes back empty would otherwise show up as a blank line in the UI with no
// indication of why.
#include <gtest/gtest.h>

#include <cstdio>

#include "Platform/Windows/ProcessorSpeedProbe.h"
#include "Platform/Windows/WindowsSystemProbe.h"

namespace tmpp::platform
{
    TEST(ProbeDiagnostic, PrintExtendedReadings)
    {
        WindowsSystemProbe probe;
        auto const info = probe.ReadProcessorInfo();

        if (!info.Success())
        {
            std::printf("ReadProcessorInfo failed: %s\n", info.GetError().Message().c_str());
            return;
        }

        auto const& value = info.Value();
        std::printf("\n--- processor ---\n");
        std::printf("model            : %s\n", value.modelName.c_str());
        std::printf("architecture     : %s\n", value.architecture.c_str());
        std::printf("base clock       : %u MHz\n", value.baseClockMhz);
        std::printf("logical          : %u\n", value.logicalProcessorCount);
        std::printf("cores            : %u\n", value.physicalCoreCount);
        std::printf("sockets          : %u\n", value.socketCount);
        std::printf("L1 / L2 / L3     : %u KB / %u KB / %llu KB\n",
                     value.l1CacheBytes / 1024,
                     value.l2CacheBytes / 1024,
                     static_cast<unsigned long long>(value.l3CacheBytes / 1024));
        std::printf("firmware virt    : %s\n", value.virtualizationFirmwareEnabled ? "yes" : "no");
        std::printf("hypervisor       : %s\n", value.hypervisorPresent ? "yes" : "no");
        std::printf("SLAT             : %s\n", value.secondLevelAddressTranslation ? "yes" : "no");
        std::printf("DEP              : %s\n", value.depAvailable ? "yes" : "no");

        ProcessorSpeedProbe speedProbe{value.baseClockMhz};
        std::printf("speed counter    : %s\n", speedProbe.Available() ? "opened" : "unavailable");
        if (speedProbe.Available())
        {
            auto const first = speedProbe.Read();
            auto const second = speedProbe.Read();
            std::printf("first read       : %s (ok=%d)\n",
                        first.Success() ? "ok" : first.GetError().Message().c_str(),
                        static_cast<int>(first.Success()));
            if (second.Success())
            {
                std::printf("speed            : %u MHz (available=%d)\n",
                            second.Value().currentMhz,
                            static_cast<int>(second.Value().available));
            }
            else
            {
                std::printf("second read      : %s\n", second.GetError().Message().c_str());
            }
        }

        auto const totals = probe.ReadTotals(1, 2, 3);
        if (totals.Success())
        {
            std::printf("uptime           : %llu s\n",
                        static_cast<unsigned long long>(totals.Value().uptimeSeconds));
        }
        std::printf("--- end ---\n\n");
    }
}