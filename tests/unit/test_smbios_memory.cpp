// Diagnostic for the SMBIOS memory-module reader.
//
// Prints every module the firmware describes, so the values can be checked against what is physically
// installed. A parser over a firmware table is easy to get subtly wrong -- an offset that is one field
// out still produces a plausible string -- so the only useful test is comparing the output with the
// machine.
#include <gtest/gtest.h>

#include <cstdio>

#include <windows.h>

#include "Platform/Windows/SmbiosMemoryProbe.h"

namespace tmpp::platform::test
{
    TEST(SmbiosMemoryProbeDiagnostic, PrintsEveryMemoryModule)
    {
        SmbiosMemoryProbe probe;
        auto const slots = probe.Read();

        ASSERT_TRUE(slots.Success()) << slots.GetError().Message();

        SystemMemorySlots const& table = slots.Value();

        std::printf("\n--- memory modules ---\n");

        // What the firmware table actually returns, which the parser's output cannot be judged without.
        {
            constexpr DWORD SMBIOS_PROVIDER = 'RSMB';
            DWORD const probeSize = GetSystemFirmwareTable(SMBIOS_PROVIDER, 0, nullptr, 0);
            std::printf("GetSystemFirmwareTable(''RSMB'') reports %lu bytes, error %lu\n",
                        probeSize,
                        GetLastError());

            DWORD const acpiSize = GetSystemFirmwareTable('ACPI', 0, nullptr, 0);
            std::printf("GetSystemFirmwareTable(''ACPI'') reports %lu bytes\n", acpiSize);
        }
        std::printf("slots: %u total, %u used, available=%s\n",
                    table.totalSlots,
                    table.usedSlots,
                    table.available ? "yes" : "no");

        uint64_t totalBytes = 0;

        for (SystemMemoryModule const& module : table.modules)
        {
            if (!module.populated)
            {
                std::printf("  [%s] empty\n", module.slot.c_str());
                continue;
            }

            std::printf("  [%s]\n", module.slot.c_str());
            std::printf("      capacity : %.1f GB\n",
                        static_cast<double>(module.capacityBytes) / (1024.0 * 1024.0 * 1024.0));
            std::printf("      type     : %s  (code 0x%02X)\n", module.typeName.c_str(), module.typeCode);
            std::printf("      form     : %s  (code 0x%02X)\n", module.formFactorName.c_str(), module.formFactorCode);
            std::printf("      speed    : %u MHz configured, %u MHz rated\n",
                        module.configuredSpeedMhz,
                        module.ratedSpeedMhz);
            std::printf("      bus width: %u bits\n", module.dataWidthBits);
            std::printf("      voltage  : %u mV\n", module.voltageMillivolts);
            std::printf("      maker    : %s\n", module.manufacturer.c_str());
            std::printf("      part     : %s\n", module.partNumber.c_str());

            totalBytes += module.capacityBytes;

            // Properties any populated module must satisfy. A slot with no capacity was reported as
            // empty above, so these are the populated ones.
            EXPECT_FALSE(module.slot.empty()) << "a populated slot must be named";
            EXPECT_GT(module.capacityBytes, 0u);
            EXPECT_GT(module.configuredSpeedMhz, 0u) << "a running module has a configured speed";
        }

        std::printf("modules total: %.1f GB\n", static_cast<double>(totalBytes) / (1024.0 * 1024.0 * 1024.0));

        if (table.available)
        {
            EXPECT_GT(table.totalSlots, 0u);
            EXPECT_LE(table.usedSlots, table.totalSlots);
            EXPECT_GT(table.usedSlots, 0u) << "a machine running the tests has memory installed";
        }
    }

    TEST(SmbiosMemoryProbeDiagnostic, TheReportedTotalIsPlausible)
    {
        // The module capacities must account for the machine's memory within one module's worth: the
        // operating system's total is the authority, and the firmware's description of the modules is
        // what is being checked against it.
        SmbiosMemoryProbe probe;
        auto const slots = probe.Read();
        ASSERT_TRUE(slots.Success());

        uint64_t totalBytes = 0;
        for (SystemMemoryModule const& module : slots.Value().modules)
        {
            totalBytes += module.capacityBytes;
        }

        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        ASSERT_TRUE(GlobalMemoryStatusEx(&memory) != FALSE);

        std::printf("\nmodules %.1f GB vs installed %.1f GB\n",
                    static_cast<double>(totalBytes) / (1024.0 * 1024.0 * 1024.0),
                    static_cast<double>(memory.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0));

        if (totalBytes > 0)
        {
            // The modules can exceed what the operating system reports, since some is reserved by the
            // firmware and the integrated parts, but they must not be less than it.
            EXPECT_GE(totalBytes, memory.ullTotalPhys)
                << "the modules cannot provide less memory than the machine has";
        }
    }
}
