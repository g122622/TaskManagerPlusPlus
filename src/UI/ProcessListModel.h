// Sorting and filtering for the process list.
//
// The render loop must stay O(visible rows), so filtering and sorting never run
// per frame. They produce an ordered vector of indices, and that vector is rebuilt
// only when the data version, the sort key or the filter text changes.
//
// Only indices are stored, not copies of the process records: a system with
// thousands of processes would otherwise copy every ProcessView on every rebuild.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Domain/ProcessModel.h"

namespace tmpp::ui
{
    /**
     * @brief Columns the process list can be sorted by.
     */
    enum class SortColumn
    {
        Name = 0,
        Pid,
        Status,
        Cpu,
        Memory,
        Disk,
        Network,
        Gpu,
        Threads,
        Handles,
        SessionId,
        ParentPid,
    };

    /**
     * @brief Sort direction.
     */
    enum class SortDirection
    {
        Ascending = 0,
        Descending,
    };

    /**
     * @brief Current sort and filter state.
     */
    struct ListQuery
    {
        SortColumn column{SortColumn::Cpu};
        SortDirection direction{SortDirection::Descending};

        /// Case-insensitive substring matched against the process name. Empty
        /// matches everything.
        std::string filter;

        /// When false, entries whose rates are not yet available are ordered last
        /// regardless of direction, so a column is not dominated by blanks during
        /// the first sample.
        bool placeUnavailableLast{true};
    };

    /**
     * @brief Owns the cached ordering of the process list.
     *
     * Not thread-safe; the UI thread owns it.
     */
    class ProcessListModel
    {
    public:
        /**
         * @brief Rebuilds the cached ordering if anything relevant changed.
         *
         * Cheap to call every frame: when the snapshot version, sort column,
         * direction and filter text are all unchanged, this returns immediately.
         *
         * @param snapshot The current published snapshot.
         * @param query Current sort and filter state.
         */
        void Update(domain::ProcessSnapshotView const& snapshot, ListQuery const& query);

        /// Indices into the snapshot's process vector, in display order.
        [[nodiscard]] std::vector<uint32_t> const& VisibleIndices() const noexcept { return m_visible; }

        /// Rows after filtering.
        [[nodiscard]] size_t VisibleCount() const noexcept { return m_visible.size(); }

        /// Total rows in the snapshot the cache was built from.
        [[nodiscard]] size_t TotalCount() const noexcept { return m_totalCount; }

        /// True when a filter is active and is hiding at least one process.
        [[nodiscard]] bool IsFiltered() const noexcept { return !m_query.filter.empty(); }

        /// The query the cache currently reflects.
        [[nodiscard]] ListQuery const& Query() const noexcept { return m_query; }

    private:
        [[nodiscard]] bool _needsRebuild(domain::ProcessSnapshotView const& snapshot, ListQuery const& query) const;

        void _rebuild(domain::ProcessSnapshotView const& snapshot);

        std::vector<uint32_t> m_visible;

        ListQuery m_query;
        uint64_t m_cachedVersion{0};
        size_t m_totalCount{0};
        bool m_valid{false};
    };
}
