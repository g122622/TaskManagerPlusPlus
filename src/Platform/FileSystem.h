// File access, expressed without leaking a platform header.
//
// Core needs to read and write its settings file. The write must be atomic: a
// power loss or a crash midway through must never leave a truncated settings
// file behind, because that would silently reset the user's configuration.
#pragma once

#include <string>
#include <string_view>

#include "Platform/Result.h"

namespace tmpp::platform
{
    /**
     * @brief Reads a whole file into memory.
     *
     * @return The file contents, or an Error when the file cannot be read.
     */
    [[nodiscard]] Result<std::string> ReadFileToString(std::string_view path);

    /**
     * @brief Writes a file atomically.
     *
     * Writes to a temporary file in the same directory and then replaces the
     * destination in one step, so a reader never observes a partial file and a
     * crash cannot truncate the previous contents.
     *
     * Any directory in @p path that does not exist is created first.
     */
    [[nodiscard]] VoidResult AtomicWriteFile(std::string_view path, std::string_view contents);

    /**
     * @brief Creates a directory and every missing parent.
     *
     * Succeeds when the directory already exists.
     */
    [[nodiscard]] VoidResult EnsureDirectory(std::string_view path);

    /**
     * @brief True when the path exists and is a regular file.
     */
    [[nodiscard]] bool FileExists(std::string_view path) noexcept;

    /**
     * @brief Renames a file, replacing any existing destination.
     *
     * Used to preserve a damaged file rather than overwriting it.
     */
    [[nodiscard]] VoidResult RenameFile(std::string_view from, std::string_view to);
}
