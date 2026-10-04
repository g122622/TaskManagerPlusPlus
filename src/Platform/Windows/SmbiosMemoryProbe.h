// SMBIOS memory-module reader.
//
// No Windows API reports what memory a machine is made of: the operating system knows how much there
// is, not which modules provide it. The firmware's SMBIOS table has it, and Windows exposes that table
// through GetSystemFirmwareTable with the 'RSMB' signature.
//
// The table is a sequence of structures, each a fixed header followed by a variable number of
// null-terminated strings and a double null terminator. Everything here is offset arithmetic over a
// buffer whose length is known but whose contents are not: the firmware is not obliged to produce
// anything sensible, so every field is bounds-checked before it is read and a malformed structure ends
// the walk rather than being trusted.
#pragma once

#include "Platform/Result.h"
#include "Platform/SystemTypes.h"

namespace tmpp::platform
{
    /**
     * @brief Reads the machine's memory modules from the firmware's SMBIOS table.
     */
    class SmbiosMemoryProbe
    {
    public:
        /**
         * @brief Reads every memory-device structure in the table.
         *
         * Returns an empty result rather than an error when the table is absent: firmware that does
         * not describe its memory is a machine to report nothing about, not a failure.
         */
        [[nodiscard]] Result<SystemMemorySlots> Read() const;
    };
}
