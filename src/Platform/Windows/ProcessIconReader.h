// Reads a process's icon as raw pixels.
//
// Icons come from the shell rather than from the image itself: the shell resolves an executable to the
// icon Windows would show for it, which accounts for per-file-type icons, shortcut overlays and the icon
// cache, none of which reading the PE resources would.
//
// Raw pixels rather than an HICON are returned so that this stays in the Platform layer. Turning pixels
// into something XAML can draw is a UI concern, and the two layers are kept apart everywhere else.
//
// The cost is a process handle and a shell lookup, both of which are expensive relative to a counter
// read. Callers are expected to ask only for what they are about to draw and to remember the answer:
// this reader does no caching of its own.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Platform/Result.h"

namespace tmpp::platform
{
    /**
     * @brief A decoded icon: its size and its pixels.
     */
    struct IconPixels
    {
        uint32_t width{0};
        uint32_t height{0};

        /// Premultiplied BGRA, top-down, `width * height * 4` bytes. Premultiplied because that is what
        /// the XAML imaging types expect, and doing it here keeps the conversion in one place.
        std::vector<uint8_t> bgra;
    };

    /**
     * @brief Reads process icons through the shell.
     */
    class ProcessIconReader
    {
    public:
        /**
         * @brief Reads the icon for a process.
         *
         * The process's executable path is resolved from its id first, because the shell looks an icon up
         * by path. A process that has exited, or one that cannot be opened, yields an error rather than an
         * empty icon: the two are different and the caller caches them differently.
         *
         * @param pid Process to read.
         * @param out Receives the pixels.
         * @param size Edge length requested, in pixels. The shell returns the closest size it has.
         */
        [[nodiscard]] Result<IconPixels> Read(uint32_t pid, int32_t size = 16) const;

        /**
         * @brief Whether an image name is one the shell cannot have an icon for.
         *
         * The idle process and the kernel's own pseudo-processes have no executable file, so asking the
         * shell about them fails every time. Recognising them here keeps that failure out of the cache and
         * off the render path.
         */
        [[nodiscard]] static bool HasNoImage(std::string const& imageName) noexcept;
    };
}
