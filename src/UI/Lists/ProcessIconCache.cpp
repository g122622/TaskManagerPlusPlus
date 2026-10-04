#include "UI/Lists/ProcessIconCache.h"

// The Windows.Foundation and Streams projections come first because robuffer.h declares IBufferByteAccess
// against their types, and the imaging projection comes after them because WriteableBitmap has to be
// complete before it is constructed here.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

// IBufferByteAccess, which is what makes a WriteableBitmap's backing store writable from C++. It is a
// WinRT interop interface rather than a projected type, so it comes from the SDK's winrt directory
// rather than from the generated projection.
#include <robuffer.h>

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <algorithm>

using winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap;

namespace tmpp::ui
{
    namespace
    {
        /// Edge length requested from the shell. Sixteen matches what Windows itself draws in its own
        /// process list at this scale, and is the size the shell's small image list already holds, so no
        /// resampling is needed.
        constexpr int32_t ICON_SIZE = 16;

        /**
         * @brief Wraps premultiplied BGRA pixels as a XAML image source.
         *
         * A WriteableBitmap written in place, rather than a SoftwareBitmapSource set asynchronously.
         *
         * The asynchronous form was tried first and took the application down: SetBitmapAsync returns an
         * awaitable, so it has to be awaited from a coroutine, and an exception escaping a
         * fire_and_forget coroutine is stowed and fails fast -- a crash inside the XAML dispatcher with
         * nothing in the log to say why. A 16 by 16 icon needs no decode, so there is nothing to be
         * gained from an asynchronous path and a great deal to be lost.
         *
         * The pixels go straight into the bitmap's backing buffer through IBufferByteAccess, which is the
         * documented way to write one. The format is stated rather than inferred: the shell returns
         * premultiplied BGRA, and handing those bytes over as straight alpha would darken every
         * translucent edge in the icon.
         */
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap _toBitmap(
            platform::IconPixels const& pixels)
        {
            if (pixels.width == 0 || pixels.height == 0 || pixels.bgra.empty())
            {
                return nullptr;
            }

            winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap bitmap(
                static_cast<int32_t>(pixels.width), static_cast<int32_t>(pixels.height));

            // The buffer is handed over by the bitmap and written through its own byte access, so the
            // pixels are copied once rather than the bitmap being rebuilt from a separate buffer. This is
            // the documented interop and the reason no SoftwareBitmap is needed at all.
            auto const buffer = bitmap.PixelBuffer();

            // IBufferByteAccess lives in the Windows::Storage::Streams namespace as a native COM
            // interface, not in the global one: it is declared by robuffer.h rather than projected. Its id
            // comes from __uuidof because the interface carries a uuid attribute rather than being a
            // projected type, so it has no guid() member.
            winrt::com_ptr<::Windows::Storage::Streams::IBufferByteAccess> access;
            winrt::check_hresult(
                buffer.as(__uuidof(::Windows::Storage::Streams::IBufferByteAccess), access.put_void()));

            // The pointer is owned by the bitmap's buffer and stays valid until the bitmap is released,
            // which is why nothing here frees it.
            uint8_t* bytes = nullptr;
            winrt::check_hresult(access->Buffer(&bytes));

            std::copy(pixels.bgra.begin(), pixels.bgra.end(), bytes);

            // The bitmap does not know its backing store changed until it is told.
            bitmap.Invalidate();
            return bitmap;
        }
    }

    winrt::Microsoft::UI::Xaml::Controls::FontIcon ProcessIconCache::PlaceholderIcon()
    {
        winrt::Microsoft::UI::Xaml::Controls::FontIcon icon;
        // Segoe Fluent Icons: a generic application window, which is the glyph the shell uses for a file
        // it has no specific icon for.
        icon.Glyph(L"\xE737");
        icon.FontSize(12.0);
        icon.Opacity(0.7);
        return icon;
    }

    bool ProcessIconCache::IsKnown(std::string const& imageName) const
    {
        return m_entries.contains(imageName);
    }

    WriteableBitmap ProcessIconCache::Lookup(std::string const& imageName)
    {
        if (imageName.empty() || platform::ProcessIconReader::HasNoImage(imageName))
        {
            // A kernel pseudo-process has no icon to find. Answering null without queueing it keeps the
            // pending queue for names the shell might actually resolve.
            return nullptr;
        }

        if (auto const found = m_entries.find(imageName); found != m_entries.end())
        {
            _touch(imageName);
            return found->second.icon;
        }

        // Not known yet. The read happens later, off the render path, so the caller draws its placeholder
        // for now rather than this frame waiting on the shell.
        return nullptr;
    }

    bool ProcessIconCache::ReadPending()
    {
        if (m_pending.empty())
        {
            return false;
        }

        bool resolvedAny = false;
        size_t const budget = (std::min)(READS_PER_REFRESH, m_pending.size());

        for (size_t i = 0; i < budget; ++i)
        {
            // Taken from the front, so a name that keeps being requested while others are ahead of it
            // cannot starve them.
            PendingRequest request = m_pending.front();
            m_pending.pop_front();
            m_pendingSet.erase(request.imageName);

            ++m_reads;

            // Read through the process that asked for it. The pid is only used to locate the executable;
            // the icon is stored under the name, so a later request from any process sharing the name hits
            // the cache without opening anything.
            WriteableBitmap source{nullptr};
            if (auto const pixels = m_reader.Read(request.pid, ICON_SIZE); pixels.Success())
            {
                source = _toBitmap(pixels.Value());
            }

            // A miss is cached as a miss. The name is remembered with no icon so the shell is asked about
            // it once rather than on every frame, which is what a process the shell cannot answer for
            // would otherwise cost.
            Entry entry;
            entry.icon = source;
            entry.hasIcon = (source != nullptr);
            entry.recency = m_recency.begin();
            m_recency.push_front(request.imageName);
            entry.recency = m_recency.begin();
            m_entries[request.imageName] = entry;

            resolvedAny = resolvedAny || entry.hasIcon;
        }

        _evictIfNeeded();
        return resolvedAny;
    }

    void ProcessIconCache::Queue(std::string imageName, uint32_t pid)
    {
        if (imageName.empty() || platform::ProcessIconReader::HasNoImage(imageName))
        {
            return;
        }

        if (m_entries.contains(imageName) || m_pendingSet.contains(imageName))
        {
            return;
        }

        m_pending.push_back(PendingRequest{std::move(imageName), pid});
    }

    void ProcessIconCache::_touch(std::string const& imageName)
    {
        auto const found = m_entries.find(imageName);
        if (found == m_entries.end())
        {
            return;
        }

        m_recency.splice(m_recency.begin(), m_recency, found->second.recency);
        found->second.recency = m_recency.begin();
    }

    void ProcessIconCache::_evictIfNeeded()
    {
        while (m_entries.size() > MAX_ENTRIES && !m_recency.empty())
        {
            std::string const& oldest = m_recency.back();
            m_entries.erase(oldest);
            m_recency.pop_back();
        }
    }
}
