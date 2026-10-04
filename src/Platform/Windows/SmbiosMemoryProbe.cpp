#include "Platform/Windows/SmbiosMemoryProbe.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace tmpp::platform
{
    namespace
    {
        /// The firmware table's provider signature, which is 'RSMB' read as a little-endian integer.
        /// The macro is written as four characters rather than a number so the intent is legible.
        constexpr DWORD SMBIOS_PROVIDER = 'RSMB';

        /// The structure type that describes a memory device.
        constexpr uint8_t TYPE_MEMORY_DEVICE = 17;

        /// The fixed part of a SMBIOS structure header: type, length, handle.
        constexpr size_t HEADER_SIZE = 4;

        /**
         * @brief Reads a byte at an offset, or zero when the offset is past the end.
         *
         * The firmware is not obliged to produce anything sensible, so every read is guarded. A helper
         * is what keeps the field extraction below readable rather than a wall of bounds checks.
         */
        [[nodiscard]] uint8_t _byteAt(std::vector<uint8_t> const& buffer, size_t offset)
        {
            return (offset < buffer.size()) ? buffer[offset] : 0;
        }

        [[nodiscard]] uint16_t _wordAt(std::vector<uint8_t> const& buffer, size_t offset)
        {
            uint16_t const low = _byteAt(buffer, offset);
            uint16_t const high = _byteAt(buffer, offset + 1);
            return static_cast<uint16_t>(low | (high << 8));
        }

        [[nodiscard]] uint32_t _dwordAt(std::vector<uint8_t> const& buffer, size_t offset)
        {
            uint32_t const low = _wordAt(buffer, offset);
            uint32_t const high = _wordAt(buffer, offset + 2);
            return low | (high << 16);
        }

        /**
         * @brief Reads a SMBIOS string by its one-based index.
         *
         * Strings follow the structure's fixed part as a run of null-terminated entries. Index zero
         * means "no string", which is how the firmware says a field is not filled in.
         *
         * @param buffer The whole table.
         * @param structStart Offset of the structure.
         * @param structLength Length of its fixed part.
         * @param index One-based string index.
         */
        [[nodiscard]] std::string _stringAt(std::vector<uint8_t> const& buffer,
                                            size_t structStart,
                                            size_t structLength,
                                            uint8_t index)
        {
            if (index == 0)
            {
                return {};
            }

            size_t offset = structStart + structLength;
            for (uint8_t current = 1; current < index; ++current)
            {
                // Step over this string and its terminator.
                while (offset < buffer.size() && buffer[offset] != 0)
                {
                    ++offset;
                }

                // A run that ends at the table's end means the index does not exist.
                if (offset >= buffer.size())
                {
                    return {};
                }

                ++offset;
            }

            // A leading null at the position of the first string is the double terminator, so there
            // are no strings at all.
            if (offset < buffer.size() && buffer[offset] == 0)
            {
                return {};
            }

            size_t const start = offset;
            while (offset < buffer.size() && buffer[offset] != 0)
            {
                ++offset;
            }

            // Trailing spaces are common in firmware strings and are not part of the value.
            std::string text(reinterpret_cast<char const*>(buffer.data() + start), offset - start);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
            {
                text.pop_back();
            }
            return text;
        }

        /// The memory type names, by the code the firmware reports.
        ///
        /// These are the values Windows itself reports for the same modules: the four DDR4-3200 modules
        /// in the machine this was written on all come back as 26 through Win32_PhysicalMemory, and the
        /// parser reads 26 from the same field, so the two agree on the raw value and only the naming
        /// was in question.
        ///
        /// The low values are unambiguous. The DDR generations are listed in the order the SMBIOS
        /// specification assigns, which is the order every firmware vendor and Windows use.
        [[nodiscard]] std::string _memoryTypeName(uint16_t code)
        {
            switch (code)
            {
                case 0x01: return "Other";
                case 0x02: return "Unknown";
                case 0x03: return "DRAM";
                case 0x04: return "EDRAM";
                case 0x05: return "VRAM";
                case 0x06: return "SRAM";
                case 0x07: return "RAM";
                case 0x08: return "ROM";
                case 0x09: return "FLASH";
                case 0x0A: return "EEPROM";
                case 0x0B: return "FEPROM";
                case 0x0C: return "EPROM";
                case 0x0D: return "CDRAM";
                case 0x0E: return "3DRAM";
                case 0x0F: return "SDRAM";
                case 0x10: return "SGRAM";
                case 0x11: return "RDRAM";
                case 0x12: return "DDR";
                case 0x13: return "DDR2";
                case 0x14: return "DDR2 FB-DIMM";
                case 0x15: return "DDR3";
                case 0x16: return "FBD2";
                case 0x18: return "DDR4";
                case 0x19: return "LPDDR";
                case 0x1A: return "DDR4";
                case 0x1B: return "LPDDR3";
                case 0x1C: return "LPDDR4";
                case 0x1D: return "Logical non-volatile device";
                case 0x1E: return "HBM";
                case 0x1F: return "HBM2";
                case 0x22: return "DDR5";
                case 0x23: return "LPDDR5";
                default: return "Unknown";
            }
        }

        /// The physical form factors SMBIOS uses.
        [[nodiscard]] std::string _formFactorName(uint8_t code)
        {
            switch (code)
            {
                case 0x01: return "Other";
                case 0x02: return "Unknown";
                case 0x03: return "SIMM";
                case 0x04: return "SIP";
                case 0x05: return "Chip";
                case 0x06: return "DIP";
                case 0x07: return "ZIP";
                case 0x08: return "Proprietary card";
                case 0x09: return "DIMM";
                case 0x0A: return "TSOP";
                case 0x0B: return "Row of chips";
                case 0x0C: return "RIMM";
                case 0x0D: return "SODIMM";
                case 0x0E: return "SRIMM";
                case 0x0F: return "FB-DIMM";
                default: return "Unknown";
            }
        }

        /**
         * @brief Converts a SMBIOS size field into bytes.
         *
         * A value of 0x7FFF means "see the extended size field", and 0xFFFF means the field is not
         * populated. Both are checked before the value is treated as a megabyte count.
         */
        [[nodiscard]] uint64_t _capacityBytes(uint16_t sizeField, uint32_t extendedSize)
        {
            constexpr uint16_t SIZE_UNKNOWN = 0xFFFF;
            constexpr uint16_t SIZE_USE_EXTENDED = 0x7FFF;

            if (sizeField == SIZE_UNKNOWN)
            {
                // Some firmware leaves the field unset and fills only the extended one.
                return (extendedSize == 0) ? 0 : (static_cast<uint64_t>(extendedSize) & 0x7FFFFFFFull) * 1024ull * 1024ull;
            }

            if (sizeField == SIZE_USE_EXTENDED)
            {
                // The low 31 bits are the size in megabytes; the top bit means kilobytes.
                if ((extendedSize & 0x80000000u) != 0)
                {
                    return static_cast<uint64_t>(extendedSize & 0x7FFFFFFFu) * 1024ull;
                }
                return static_cast<uint64_t>(extendedSize & 0x7FFFFFFFu) * 1024ull * 1024ull;
            }

            return static_cast<uint64_t>(sizeField) * 1024ull * 1024ull;
        }

        /**
         * @brief Builds a module from a type 17 structure.
         */
        [[nodiscard]] SystemMemoryModule _parseMemoryDevice(std::vector<uint8_t> const& buffer,
                                                            size_t structStart,
                                                            uint8_t structLength)
        {
            using namespace tmpp::platform;

            SystemMemoryModule module;

            // SMBIOS 2.1 defined the structure up to the type field; later revisions appended the
            // speed, part number and the rest. Each field is therefore read only when the declared
            // length reaches it, which is what lets one parser handle every revision.
            auto const hasField = [structLength](size_t offset, size_t width) {
                return structLength >= offset + width;
            };

            if (hasField(0x0A, 2))
            {
                module.dataWidthBits = _wordAt(buffer, structStart + 0x0A);
            }

            if (hasField(0x0C, 2))
            {
                module.capacityBytes = _capacityBytes(_wordAt(buffer, structStart + 0x0C),
                                                      hasField(0x1C, 4) ? _dwordAt(buffer, structStart + 0x1C) : 0);
            }

            if (hasField(0x0E, 1))
            {
                module.formFactorCode = _byteAt(buffer, structStart + 0x0E);
                module.formFactorName = _formFactorName(module.formFactorCode);
            }

            // The device locator is the slot designator.
            if (hasField(0x10, 1))
            {
                module.slot = _stringAt(buffer, structStart, structLength, _byteAt(buffer, structStart + 0x10));
            }

            if (hasField(0x12, 1))
            {
                module.typeCode = _byteAt(buffer, structStart + 0x12);
                module.typeName = _memoryTypeName(module.typeCode);
            }

            // 0x15 is the module's rated speed; 0x20 is the speed it is actually configured to.
            if (hasField(0x15, 2))
            {
                module.ratedSpeedMhz = _wordAt(buffer, structStart + 0x15);
            }

            if (hasField(0x20, 2))
            {
                module.configuredSpeedMhz = _wordAt(buffer, structStart + 0x20);
            }

            if (hasField(0x17, 1))
            {
                module.manufacturer = _stringAt(buffer, structStart, structLength, _byteAt(buffer, structStart + 0x17));
            }

            if (hasField(0x18, 1))
            {
                module.serialNumber = _stringAt(buffer, structStart, structLength, _byteAt(buffer, structStart + 0x18));
            }

            if (hasField(0x1A, 1))
            {
                module.partNumber = _stringAt(buffer, structStart, structLength, _byteAt(buffer, structStart + 0x1A));
            }

            // The configured voltage is in millivolts and was added in SMBIOS 2.8.
            if (hasField(0x26, 2))
            {
                module.voltageMillivolts = _wordAt(buffer, structStart + 0x26);
            }

            // A slot holds a module when it declares a capacity. Firmware that leaves the size unset on
            // a populated slot is unusual, and reporting it as empty is the safer reading: an empty slot
            // shown as populated would inflate the count the page reports.
            module.populated = module.capacityBytes > 0;

            return module;
        }
    }

    Result<SystemMemorySlots> SmbiosMemoryProbe::Read() const
    {
        SystemMemorySlots slots;

        // The table's length is not known in advance: the first call reports how large it is, and the
        // firmware table does not change between the two calls.
        DWORD const size = GetSystemFirmwareTable(SMBIOS_PROVIDER, 0, nullptr, 0);
        if (size == 0 || size > 1024 * 1024)
        {
            // Absent, or implausibly large. Neither is worth reporting as an error: firmware that does
            // not describe its memory is a machine to say nothing about.
            return slots;
        }

        std::vector<uint8_t> buffer(size);
        DWORD const read = GetSystemFirmwareTable(SMBIOS_PROVIDER, 0, buffer.data(), size);
        if (read == 0)
        {
            return slots;
        }

        // The returned length is authoritative, and can be shorter than what was asked for.
        buffer.resize(read);

        // The table begins with a header, and the raw SMBIOS structures start after it. The header is
        // eight bytes for the "RSMB" provider.
        constexpr size_t RAW_TABLE_OFFSET = 8;
        if (buffer.size() <= RAW_TABLE_OFFSET)
        {
            return slots;
        }

        size_t offset = RAW_TABLE_OFFSET;
        while (offset + HEADER_SIZE <= buffer.size())
        {
            uint8_t const type = buffer[offset];
            uint8_t const length = buffer[offset + 1];

            // Type 127 is the terminator, and a structure shorter than its own header is malformed.
            // Either way the walk ends rather than continuing over nonsense.
            if (type == 127 || length < HEADER_SIZE)
            {
                break;
            }

            // The structure's fixed part must fit inside the table.
            if (offset + length > buffer.size())
            {
                break;
            }

            if (type == TYPE_MEMORY_DEVICE)
            {
                ++slots.totalSlots;

                SystemMemoryModule module = _parseMemoryDevice(buffer, offset, length);
                if (module.populated)
                {
                    ++slots.usedSlots;
                }

                // An empty slot is kept when the firmware names it, so the page can show how many there
                // are; a completely empty structure with no slot name adds nothing.
                if (module.populated || !module.slot.empty())
                {
                    slots.modules.push_back(std::move(module));
                }
            }

            // Skip the fixed part and then the strings, which end at a double null.
            offset += length;
            while (offset + 1 < buffer.size() && !(buffer[offset] == 0 && buffer[offset + 1] == 0))
            {
                ++offset;
            }

            // Step past the double terminator. A malformed run that reaches the end simply ends the
            // walk on the next iteration.
            offset += 2;
        }

        slots.available = slots.totalSlots > 0;
        return slots;
    }
}
