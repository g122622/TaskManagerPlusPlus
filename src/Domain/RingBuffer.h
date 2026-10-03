// Fixed-capacity history buffer.
//
// Chart history must not grow without bound: the application is expected to stay
// resident for days, and the sampling interval can be as fast as 500 ms. This
// buffer owns a fixed allocation for its whole lifetime, so history memory is
// constant regardless of uptime.
#pragma once

#include <cstddef>
#include <vector>

namespace tmpp::domain
{
    /**
     * @brief A bounded ring buffer holding the most recent N samples.
     *
     * Overwrites the oldest entry once full. Iteration yields samples in
     * chronological order (oldest first), which is the order charts draw in.
     */
    template <typename T>
    class RingBuffer
    {
    public:
        /**
         * @brief Constructs a buffer with a fixed capacity.
         *
         * @param capacity Number of samples to retain. A capacity of 0 is
         *        promoted to 1 so that Push followed by Latest is always safe.
         */
        explicit RingBuffer(size_t capacity)
            : m_storage(capacity == 0 ? 1 : capacity), m_capacity(m_storage.size())
        {
        }

        /// Appends a sample, overwriting the oldest when full.
        void Push(T value)
        {
            m_storage[m_head] = std::move(value);
            m_head = (m_head + 1) % m_capacity;
            if (m_size < m_capacity)
            {
                ++m_size;
            }
        }

        /// Discards all samples without releasing the allocation.
        void Clear() noexcept
        {
            m_head = 0;
            m_size = 0;
        }

        [[nodiscard]] size_t Size() const noexcept { return m_size; }

        [[nodiscard]] size_t Capacity() const noexcept { return m_capacity; }

        [[nodiscard]] bool Empty() const noexcept { return m_size == 0; }

        [[nodiscard]] bool Full() const noexcept { return m_size == m_capacity; }

        /**
         * @brief Accesses a sample by age, oldest first.
         *
         * @param index 0 is the oldest retained sample, Size()-1 the newest.
         */
        [[nodiscard]] T const& At(size_t index) const
        {
            return m_storage[_physicalIndex(index)];
        }

        /**
         * @brief The most recently pushed sample.
         *
         * Undefined when the buffer is empty; callers check Empty() first, which
         * the sampler always can because it pushes before it reads.
         */
        [[nodiscard]] T const& Latest() const { return At(m_size - 1); }

        /**
         * @brief The oldest retained sample.
         */
        [[nodiscard]] T const& Oldest() const { return At(0); }

        /**
         * @brief Copies the retained samples into a vector, oldest first.
         *
         * Used when a render pass needs a stable snapshot rather than an index.
         */
        [[nodiscard]] std::vector<T> ToVector() const
        {
            std::vector<T> result;
            result.reserve(m_size);
            for (size_t i = 0; i < m_size; ++i)
            {
                result.push_back(At(i));
            }
            return result;
        }

    private:
        /**
         * @brief Maps a logical (oldest-first) index to its storage slot.
         */
        [[nodiscard]] size_t _physicalIndex(size_t logicalIndex) const noexcept
        {
            // When the buffer has wrapped, the oldest sample sits at m_head; before
            // that it sits at 0.
            size_t const start = (m_size == m_capacity) ? m_head : 0;
            return (start + logicalIndex) % m_capacity;
        }

        std::vector<T> m_storage;
        size_t m_capacity{0};
        size_t m_head{0}; ///< Next slot to write.
        size_t m_size{0};
    };
}
