#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace tmpp::ui
{
    [[nodiscard]] inline bool IsMemoryCompressionImage(std::string_view image) noexcept
    {
        constexpr std::string_view name = "memory compression";
        if (image.size() != name.size()) return false;
        for (size_t i = 0; i < image.size(); ++i)
        {
            char const character = image[i] >= 'A' && image[i] <= 'Z' ? image[i] + ('a' - 'A') : image[i];
            if (character != name[i]) return false;
        }
        return true;
    }

    class MemoryHeatScale
    {
    public:
        void Add(uint64_t bytes, std::string_view image) noexcept
        {
            m_peak = (std::max)(m_peak, bytes);
            if (!IsMemoryCompressionImage(image)) m_regularPeak = (std::max)(m_regularPeak, bytes);
        }

        /// Compression at rank one saturates alongside the largest ordinary process.
        [[nodiscard]] uint64_t Peak() const noexcept { return m_regularPeak > 0 ? m_regularPeak : m_peak; }

    private:
        uint64_t m_peak{0};
        uint64_t m_regularPeak{0};
    };
}
