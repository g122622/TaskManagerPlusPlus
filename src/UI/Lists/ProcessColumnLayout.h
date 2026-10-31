#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace tmpp::ui::columns
{
    inline constexpr size_t Count = 21;

    [[nodiscard]] inline std::vector<uint32_t> NormalizeOrder(std::vector<uint32_t> const& requested,
                                                            size_t count = Count)
    {
        std::vector<uint32_t> result;
        result.reserve(count);
        for (auto const column : requested)
            if (column < count && std::find(result.begin(), result.end(), column) == result.end())
                result.push_back(column);
        for (uint32_t column = 0; column < count; ++column)
            if (std::find(result.begin(), result.end(), column) == result.end()) result.push_back(column);
        return result;
    }

    [[nodiscard]] inline bool Move(std::vector<uint32_t>& order, uint32_t source, uint32_t target, bool after)
    {
        auto const from = std::find(order.begin(), order.end(), source);
        auto const to = std::find(order.begin(), order.end(), target);
        if (from == order.end() || to == order.end() || source == target) return false;
        auto const before = order;
        order.erase(from);
        auto destination = std::find(order.begin(), order.end(), target);
        if (after) ++destination;
        order.insert(destination, source);
        return order != before;
    }
}
