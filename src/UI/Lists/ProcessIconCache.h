// A bounded, budgeted cache of process icons.
//
// Reading an icon costs two calls the rest of the sampling path does not make: opening the process to
// resolve its executable path, and a shell lookup. Measured on this machine a cold read is about 15
// milliseconds, which is far too slow to run inside a row binder: a scroll revealing twenty rows would
// stall the UI thread for a third of a second.
//
// Three decisions keep that cost off the render path.
//
//   * Nothing is read during a lookup. A miss returns the placeholder and records the request; the reads
//     happen later, a few per refresh, so no single frame can block on the shell however many new
//     processes appear at once.
//
//   * The cache is keyed by image name, not by executable path. A path can only be had by opening the
//     process, so keying by path would mean paying for the open before the cache could even be consulted
//     -- which is the cost the cache exists to avoid. Two different files sharing a basename therefore
//     share an icon, which is a rare case and a harmless one: both get a real icon either way.
//
//   * The cache is bounded and evicts least-recently-used, so a machine that starts and stops thousands
//     of distinct programs cannot grow it without limit. A name the shell cannot answer for is cached as
//     a miss, so it is asked about once rather than on every frame.
#pragma once

#include <cstdint>
#include <deque>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "UI/WinRTUI.h"

#include "Platform/Windows/ProcessIconReader.h"

namespace tmpp::ui
{
    /**
     * @brief Icons for the process list, keyed by image name.
     *
     * Not thread-safe; the UI thread owns it.
     */
    class ProcessIconCache
    {
    public:
        /**
         * @brief The most icons to hold.
         *
         * A few hundred covers the distinct executables a normal machine runs, and each is a 16 by 16
         * premultiplied BGRA bitmap of about a kilobyte, so the whole cache stays well under a megabyte.
         * Past this the least recently used entry is dropped rather than the cache growing with the number
         * of programs the machine has ever run.
         */
        static constexpr size_t MAX_ENTRIES = 512;

        /**
         * @brief How many icons to read per call to ReadPending.
         *
         * The refresh timer runs every 100 milliseconds, so eight per call is eighty reads a second: a
         * machine with eighty distinct executables is fully populated in a second, and no single frame
         * spends more than about a hundred milliseconds on the shell. Lower would populate too slowly to
         * feel immediate; higher would start to be felt as a stutter.
         */
        static constexpr size_t READS_PER_REFRESH = 8;

        /**
         * @brief The icon drawn while an icon is pending, or for a process that has none.
         *
         * A generic program glyph rather than an empty space: a row with a gap where every other row has
         * an image reads as a rendering fault, and the kernel's own pseudo-processes genuinely have no
         * icon file.
         */
        [[nodiscard]] static winrt::Microsoft::UI::Xaml::Controls::FontIcon PlaceholderIcon();

        /**
         * @brief The icon for an image name, or null when it is not known yet.
         *
         * Never reads anything. A name that has not been seen is queued for the next ReadPending and null
         * is returned, so the caller draws its placeholder and carries on.
         */
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap Lookup(
            std::string const& imageName);

        /**
         * @brief Queues an image name to be read on the next ReadPending.
         *
         * Called by the row binder for a name it found neither cached nor already queued. Queueing is
         * cheap and unconditional: the row carries on with its placeholder, and the read happens between
         * frames.
         *
         * @param imageName The name to read.
         * @param pid A process currently running that name, used to locate the executable. Any process
         *        with the name will do; the first one seen is as good as another.
         */
        void Queue(std::string imageName, uint32_t pid);

        /// True when the name is known, whether or not the shell had an icon for it.
        [[nodiscard]] bool IsKnown(std::string const& imageName) const;

        /**
         * @brief Reads a few of the queued icons.
         *
         * Bounded by READS_PER_REFRESH. Call once per refresh, not per row.
         *
         * @return True when at least one icon was resolved, so the caller can repaint the visible rows.
         */
        bool ReadPending();

        /// Number of icons currently held.
        [[nodiscard]] size_t Count() const noexcept { return m_entries.size(); }

        /// Number of reads actually performed, as opposed to lookups served from the cache.
        [[nodiscard]] size_t ReadsPerformed() const noexcept { return m_reads; }

        /// Number of image names waiting to be read.
        [[nodiscard]] size_t PendingCount() const noexcept { return m_pending.size(); }

    private:
        /// One cached outcome: the icon when the shell had one, and whether it did.
        ///
        /// The flag is separate from the bitmap because a cached miss has to be told apart from a name
        /// that has not been read yet: the first must not be queued again, and the second is not in this
        /// map at all, since a name only enters it once it has been read.
        struct Entry
        {
            winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap icon{nullptr};
            bool hasIcon{false};
            std::list<std::string>::iterator recency;
        };

        /// An image name waiting to be read, with a process that is currently running it.
        struct PendingRequest
        {
            std::string imageName;
            uint32_t pid{0};
        };

        /// Marks a name as most recently used.
        void _touch(std::string const& imageName);

        /// Drops least recently used entries until the cache is within its bound.
        void _evictIfNeeded();

        platform::ProcessIconReader m_reader;

        std::unordered_map<std::string, Entry> m_entries;

        /// Most recently used first, least recently used last.
        std::list<std::string> m_recency;

        /// Names seen but not yet read, oldest first. A deque because entries are taken from the front
        /// while new ones are appended at the back.
        std::deque<PendingRequest> m_pending;

        /// Names waiting, so a name is not queued twice while it is in flight.
        std::unordered_set<std::string> m_pendingSet;

        size_t m_reads{0};
    };
}
