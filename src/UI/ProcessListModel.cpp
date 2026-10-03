#include "UI/ProcessListModel.h"

#include <algorithm>
#include <cctype>

namespace tmpp::ui
{
    namespace
    {
        /**
         * @brief Lowercases an ASCII string for case-insensitive comparison.
         *
         * Deliberately ASCII-only: it is applied to image names and the user's
         * filter, and a full Unicode fold would cost more than it buys here. The
         * comparison still works correctly for non-ASCII input, it just compares
         * those code units exactly.
         */
        [[nodiscard]] std::string _toLowerAscii(std::string const& text)
        {
            std::string lowered = text;
            for (char& character : lowered)
            {
                auto const byte = static_cast<unsigned char>(character);
                if (byte < 0x80)
                {
                    character = static_cast<char>(std::tolower(byte));
                }
            }
            return lowered;
        }

        /**
         * @brief Reads the sort key's numeric value from a process.
         *
         * Memory is compared as working set; the column shows that value, so
         * sorting must agree with what is displayed.
         */
        [[nodiscard]] double _numericKey(domain::ProcessView const& process, SortColumn column) noexcept
        {
            switch (column)
            {
                case SortColumn::Pid:
                    return static_cast<double>(process.identity.pid);
                case SortColumn::Cpu:
                    return process.cpuPercent;
                case SortColumn::Memory:
                    return static_cast<double>(process.memory.workingSetSize);
                case SortColumn::Disk:
                    return process.diskReadBytesPerSec + process.diskWriteBytesPerSec;
                case SortColumn::Threads:
                    return static_cast<double>(process.threadCount);
                case SortColumn::Handles:
                    return static_cast<double>(process.handleCount);
                case SortColumn::SessionId:
                    return static_cast<double>(process.sessionId);
                case SortColumn::ParentPid:
                    return static_cast<double>(process.parentPid);
                case SortColumn::Network:
                case SortColumn::Gpu:
                    // Not collected yet. Sorting by these yields a stable order
                    // rather than a meaningless one; the columns are hidden until
                    // the data exists (see docs/ROADMAP.md).
                    return 0.0;
                case SortColumn::Status:
                case SortColumn::Name:
                default:
                    return 0.0;
            }
        }
    }

    bool ProcessListModel::_needsRebuild(domain::ProcessSnapshotView const& snapshot, ListQuery const& query) const
    {
        if (!m_valid)
        {
            return true;
        }

        // The version changes on every publication, so an unchanged version means
        // the underlying rows cannot have moved or changed value.
        if (m_cachedVersion != snapshot.version)
        {
            return true;
        }

        return m_query.column != query.column || m_query.direction != query.direction ||
               m_query.filter != query.filter || m_query.placeUnavailableLast != query.placeUnavailableLast;
    }

    void ProcessListModel::Update(domain::ProcessSnapshotView const& snapshot, ListQuery const& query)
    {
        if (!_needsRebuild(snapshot, query))
        {
            return;
        }

        m_query = query;
        m_cachedVersion = snapshot.version;
        m_totalCount = snapshot.processes.size();

        _rebuild(snapshot);
        m_valid = true;
    }

    void ProcessListModel::_rebuild(domain::ProcessSnapshotView const& snapshot)
    {
        m_visible.clear();
        m_visible.reserve(snapshot.processes.size());

        std::string const filterLower = _toLowerAscii(m_query.filter);
        bool const hasFilter = !filterLower.empty();

        for (uint32_t i = 0; i < snapshot.processes.size(); ++i)
        {
            if (hasFilter)
            {
                std::string const nameLower = _toLowerAscii(snapshot.processes[i].imageName);
                if (nameLower.find(filterLower) == std::string::npos)
                {
                    continue;
                }
            }
            m_visible.push_back(i);
        }

        if (m_query.column == SortColumn::Name)
        {
            // Names sort as text, case-insensitively, so "chrome" and "Chrome"
            // do not end up in opposite halves of the list.
            bool const ascending = m_query.direction == SortDirection::Ascending;
            std::stable_sort(m_visible.begin(), m_visible.end(), [&snapshot, ascending](uint32_t left, uint32_t right) {
                std::string const leftName = _toLowerAscii(snapshot.processes[left].imageName);
                std::string const rightName = _toLowerAscii(snapshot.processes[right].imageName);
                return ascending ? (leftName < rightName) : (rightName < leftName);
            });
            return;
        }

        bool const ascending = m_query.direction == SortDirection::Ascending;
        std::stable_sort(m_visible.begin(),
                         m_visible.end(),
                         [this, &snapshot, ascending](uint32_t left, uint32_t right) {
                             domain::ProcessView const& a = snapshot.processes[left];
                             domain::ProcessView const& b = snapshot.processes[right];

                             // During the first sample no process has rates yet.
                             // Ordering those rows consistently keeps the list from
                             // jumping around while the values settle.
                             if (m_query.placeUnavailableLast && a.ratesUnavailable != b.ratesUnavailable)
                             {
                                 return b.ratesUnavailable;
                             }

                             double const keyA = _numericKey(a, m_query.column);
                             double const keyB = _numericKey(b, m_query.column);

                             if (keyA == keyB)
                             {
                                 // Ties fall back to the PID so the order is stable
                                 // between frames rather than depending on the
                                 // snapshot's incidental ordering.
                                 return a.identity.pid < b.identity.pid;
                             }
                             return ascending ? (keyA < keyB) : (keyA > keyB);
                         });
    }
}
