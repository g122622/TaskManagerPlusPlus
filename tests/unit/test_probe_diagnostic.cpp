// Temporary diagnostic: prints the extended system probe readings.
//
// Not part of the suite. It exists to confirm what this machine actually reports
// before the performance page is built around those fields, because a field that
// silently comes back empty would otherwise show up as a blank line in the UI with no
// indication of why.
#include <gtest/gtest.h>

#include <cstdio>

#include <windows.h>

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

        auto const composition = probe.ReadMemoryComposition();
        std::printf("--- memory composition ---\n");
        if (composition.Success())
        {
            auto const& m = composition.Value();
            double const mb = 1024.0 * 1024.0;
            std::printf("available        : %s\n", m.available ? "yes" : "no");
            std::printf("page size        : %llu\n", static_cast<unsigned long long>(m.pageSize));
            std::printf("in use           : %.1f MB\n", static_cast<double>(m.inUseBytes) / mb);
            std::printf("modified         : %.1f MB\n", static_cast<double>(m.modifiedBytes) / mb);
            std::printf("standby (cached) : %.1f MB\n", static_cast<double>(m.standbyBytes) / mb);
            std::printf("free             : %.1f MB\n", static_cast<double>(m.freeBytes) / mb);
            std::printf("total accounted  : %.1f MB\n", static_cast<double>(m.Total()) / mb);

            MEMORYSTATUSEX mem{};
            mem.dwLength = sizeof(mem);
            if (GlobalMemoryStatusEx(&mem) != 0)
            {
                std::printf("installed        : %.1f MB\n", static_cast<double>(mem.ullTotalPhys) / mb);
                long long const diff = static_cast<long long>(m.Total()) - static_cast<long long>(mem.ullTotalPhys);
                std::printf("difference       : %lld MB (should be near zero)\n", diff / (1024 * 1024));
            }
        }
        else
        {
            std::printf("composition read failed: %s\n", composition.GetError().Message().c_str());
        }
        std::printf("--- end ---\n\n");

        auto const totals = probe.ReadTotals(1, 2, 3);
        if (totals.Success())
        {
            std::printf("uptime           : %llu s\n",
                        static_cast<unsigned long long>(totals.Value().uptimeSeconds));
        }
        std::printf("--- end ---\n\n");
    }
}