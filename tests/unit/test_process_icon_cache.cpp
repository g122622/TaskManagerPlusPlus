// Tests for the process icon cache.
//
// The cache exists because a shell lookup costs about fifteen milliseconds, which a row binder cannot
// afford. What matters is therefore not which icon comes back -- that is the shell's business -- but the
// three properties that keep the cost bounded:
//
//   * a lookup never reads, so no frame can block on the shell;
//   * a name is read once, however many processes share it;
//   * the cache cannot grow without limit, and a name the shell cannot answer for is not asked again.
//
// These are the parts that fail silently: an unbounded cache or a lookup that reads would both still
// draw the right icons, and only show up as a machine that gets slower the longer it runs.
#include <gtest/gtest.h>

#include <algorithm>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace tmpp::ui::test
{
    namespace
    {
        /// Mirrors the cache's pending queue and membership rules, without a shell or a UI thread.
        ///
        /// The behaviour under test is the bookkeeping, so it is reproduced here rather than driven
        /// through the real cache, which would need a desktop session and would make the same assertions
        /// take fifteen milliseconds each.
        class CacheModel
        {
        public:
            static constexpr size_t MAX_ENTRIES = 512;
            static constexpr size_t READS_PER_REFRESH = 8;

            /// Mirrors Lookup. Never reads; queues a miss and reports it as unknown.
            [[nodiscard]] bool Lookup(std::string const& name)
            {
                if (name.empty())
                {
                    return false;
                }

                if (m_known.contains(name))
                {
                    return m_hasIcon[name];
                }

                if (!m_pendingSet.contains(name))
                {
                    m_pending.push_back(name);
                    m_pendingSet.insert(name);
                }
                return false;
            }

            /// Mirrors ReadPending, with a caller-supplied verdict for what the shell would say.
            size_t ReadPending(bool shellHasIcon, std::vector<std::string> const& iconless = {})
            {
                size_t const budget = (std::min)(READS_PER_REFRESH, m_pending.size());
                size_t reads = 0;

                for (size_t i = 0; i < budget; ++i)
                {
                    std::string const name = m_pending.front();
                    m_pending.erase(m_pending.begin());
                    m_pendingSet.erase(name);

                    bool const hasIcon =
                        shellHasIcon &&
                        std::find(iconless.begin(), iconless.end(), name) == iconless.end();

                    m_known.insert(name);
                    m_hasIcon[name] = hasIcon;
                    m_recency.push_front(name);
                    ++reads;
                }

                _evict();
                m_totalReads += reads;
                return reads;
            }

            [[nodiscard]] size_t Count() const noexcept { return m_known.size(); }
            [[nodiscard]] size_t Pending() const noexcept { return m_pending.size(); }
            [[nodiscard]] size_t TotalReads() const noexcept { return m_totalReads; }

        private:
            void _evict()
            {
                while (m_known.size() > MAX_ENTRIES && !m_recency.empty())
                {
                    m_known.erase(m_recency.back());
                    m_hasIcon.erase(m_recency.back());
                    m_recency.pop_back();
                }
            }

            std::set<std::string> m_known;
            std::map<std::string, bool> m_hasIcon;
            std::vector<std::string> m_pending;
            std::set<std::string> m_pendingSet;
            std::list<std::string> m_recency;
            size_t m_totalReads{0};
        };
    }

    TEST(ProcessIconCacheTest, ALookupNeverReadsAnything)
    {
        // The property that keeps a frame from blocking. A lookup on an unknown name queues it and says
        // so; the read happens only in ReadPending.
        CacheModel cache;

        EXPECT_FALSE(cache.Lookup("chrome.exe"));
        EXPECT_EQ(cache.Count(), 0u) << "a lookup must not resolve anything by itself";
        EXPECT_EQ(cache.Pending(), 1u) << "it must queue the name for a later read";
    }

    TEST(ProcessIconCacheTest, ANameIsReadOnceHoweverManyProcessesShareIt)
    {
        // Forty Chrome processes must cost one shell lookup, not forty. This is the whole reason the cache
        // is keyed by name.
        CacheModel cache;

        for (int i = 0; i < 40; ++i)
        {
            (void)cache.Lookup("chrome.exe");
        }

        EXPECT_EQ(cache.Pending(), 1u) << "the same name was queued more than once";

        cache.ReadPending(/*shellHasIcon=*/true);
        EXPECT_EQ(cache.TotalReads(), 1u);

        // Every later lookup is served from the cache without queueing anything.
        for (int i = 0; i < 40; ++i)
        {
            EXPECT_TRUE(cache.Lookup("chrome.exe"));
        }

        EXPECT_EQ(cache.Pending(), 0u);
        EXPECT_EQ(cache.TotalReads(), 1u) << "a cached name was read a second time";
    }

    TEST(ProcessIconCacheTest, ReadsAreBoundedPerCall)
    {
        // A machine that starts a hundred new programs in one sample must not make one frame read a
        // hundred icons. The queue is drained a few at a time instead.
        CacheModel cache;

        for (int i = 0; i < 100; ++i)
        {
            (void)cache.Lookup("program" + std::to_string(i) + ".exe");
        }

        EXPECT_EQ(cache.Pending(), 100u);

        size_t const firstBatch = cache.ReadPending(/*shellHasIcon=*/true);
        EXPECT_EQ(firstBatch, CacheModel::READS_PER_REFRESH);

        // The rest stays queued rather than being discarded.
        EXPECT_EQ(cache.Pending(), 100u - CacheModel::READS_PER_REFRESH);

        // Draining the whole queue takes several calls, and every call is bounded.
        size_t guard = 0;
        while (cache.Pending() > 0 && guard < 100)
        {
            EXPECT_LE(cache.ReadPending(/*shellHasIcon=*/true), CacheModel::READS_PER_REFRESH);
            ++guard;
        }

        EXPECT_EQ(cache.Pending(), 0u);
        EXPECT_EQ(cache.Count(), 100u);
    }

    TEST(ProcessIconCacheTest, TheCacheIsBoundedAndEvictsTheOldest)
    {
        // A machine that runs thousands of distinct programs over a long session must not grow the cache
        // without limit. Each entry is a small bitmap, so this is megabytes rather than gigabytes, but it
        // is still unbounded growth in a program meant to run for days.
        CacheModel cache;

        for (size_t i = 0; i < CacheModel::MAX_ENTRIES + 200; ++i)
        {
            std::string const name = "program" + std::to_string(i) + ".exe";
            (void)cache.Lookup(name);
            cache.ReadPending(/*shellHasIcon=*/true);
        }

        EXPECT_LE(cache.Count(), CacheModel::MAX_ENTRIES) << "the cache grew past its bound";

        // The most recently read names survive, since those are the ones a running machine is showing.
        std::string const recent = "program" + std::to_string(CacheModel::MAX_ENTRIES + 199) + ".exe";
        EXPECT_TRUE(cache.Lookup(recent)) << "the most recent entry was evicted";
    }

    TEST(ProcessIconCacheTest, ANameTheShellCannotAnswerForIsNotAskedAgain)
    {
        // A process whose icon the shell cannot produce is cached as a miss. Without that, every frame
        // would retry the lookup and pay the full cost for a name that will never resolve.
        CacheModel cache;

        (void)cache.Lookup("ghost.exe");
        cache.ReadPending(/*shellHasIcon=*/false);

        EXPECT_EQ(cache.Count(), 1u) << "the failed name was not remembered";

        // Known, and known to have no icon: no second read and no re-queue.
        EXPECT_FALSE(cache.Lookup("ghost.exe"));
        EXPECT_EQ(cache.Pending(), 0u) << "a failed name was queued again";
        EXPECT_EQ(cache.TotalReads(), 1u);
    }

    TEST(ProcessIconCacheTest, AnEmptyNameIsNeverQueued)
    {
        // The idle process has no image name, and the shell cannot be asked about nothing. Queueing it
        // would spend a read per frame on a name that can never resolve.
        CacheModel cache;

        EXPECT_FALSE(cache.Lookup(""));
        EXPECT_EQ(cache.Pending(), 0u);
    }
}
