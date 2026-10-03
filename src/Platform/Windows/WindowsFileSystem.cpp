#include "Platform/FileSystem.h"

#include "Platform/Windows/WindowsString.h"

#include <windows.h>

#include <fstream>
#include <sstream>

namespace tmpp::platform
{
    namespace
    {
        [[nodiscard]] Error _lastError(std::string_view operation, std::string_view path)
        {
            return Error{ErrorCode::NativeFailure,
                         std::string(operation) + " failed with error " + std::to_string(GetLastError()),
                         std::string(path)};
        }

        /**
         * @brief Creates the parent directories of a file path.
         */
        [[nodiscard]] VoidResult _ensureParentDirectory(std::string_view path)
        {
            size_t const separator = path.find_last_of("\\/");
            if (separator == std::string_view::npos)
            {
                return VoidResult::Ok();
            }
            return EnsureDirectory(path.substr(0, separator));
        }
    }

    Result<std::string> ReadFileToString(std::string_view path)
    {
        std::wstring const widePath = ToUtf16(path);

        std::ifstream stream(widePath, std::ios::binary);
        if (!stream.is_open())
        {
            return Error{ErrorCode::NotFound, "Could not open file for reading", std::string(path)};
        }

        std::ostringstream contents;
        contents << stream.rdbuf();

        // A read that fails partway leaves the stream in a bad state; reporting the
        // partial content as success would let a damaged settings file parse into a
        // misleading half-configuration.
        if (stream.bad())
        {
            return Error{ErrorCode::NativeFailure, "Read failed", std::string(path)};
        }

        return contents.str();
    }

    VoidResult AtomicWriteFile(std::string_view path, std::string_view contents)
    {
        if (VoidResult const parent = _ensureParentDirectory(path); !parent.Success())
        {
            return parent;
        }

        std::string temporaryPath{path};
        temporaryPath += ".tmp";

        std::wstring const wideTemporaryPath = ToUtf16(temporaryPath);
        {
            std::ofstream stream(wideTemporaryPath, std::ios::binary | std::ios::trunc);
            if (!stream.is_open())
            {
                return Error{ErrorCode::AccessDenied, "Could not open temporary file for writing", temporaryPath};
            }

            stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            stream.flush();

            if (!stream.good())
            {
                stream.close();
                DeleteFileW(wideTemporaryPath.c_str());
                return Error{ErrorCode::NativeFailure, "Write to temporary file failed", temporaryPath};
            }
            // The stream is closed here, before the rename, so the data is on its
            // way to disk before the destination is replaced.
        }

        std::wstring const widePath = ToUtf16(path);

        // MOVEFILE_REPLACE_EXISTING makes this a single-step replacement, so a
        // reader sees either the old file or the new one, never a partial write.
        if (MoveFileExW(wideTemporaryPath.c_str(), widePath.c_str(), MOVEFILE_REPLACE_EXISTING) == 0)
        {
            Error const error = _lastError("MoveFileEx", path);
            DeleteFileW(wideTemporaryPath.c_str());
            return error;
        }

        return VoidResult::Ok();
    }

    VoidResult EnsureDirectory(std::string_view path)
    {
        std::wstring const widePath = ToUtf16(path);

        if (CreateDirectoryW(widePath.c_str(), nullptr) != 0)
        {
            return VoidResult::Ok();
        }

        // Already existing is success: callers want "the directory is there", and
        // the check is inherently racy if done separately.
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            return VoidResult::Ok();
        }

        // CreateDirectoryW does not create intermediate directories, so a nested
        // path fails unless the parents are created first. Walk up to the nearest
        // existing ancestor and create the chain on the way back down.
        if (GetLastError() == ERROR_PATH_NOT_FOUND)
        {
            std::wstring parent = widePath;
            // Drop a trailing separator so the search finds the real parent.
            while (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/'))
            {
                parent.pop_back();
            }

            auto const separator = parent.find_last_of(L"\\/");
            if (separator == std::wstring::npos)
            {
                return _lastError("CreateDirectory", path);
            }

            VoidResult const createdParent = EnsureDirectory(ToUtf8(parent.substr(0, separator)));
            if (!createdParent.Success())
            {
                return createdParent;
            }

            if (CreateDirectoryW(widePath.c_str(), nullptr) != 0)
            {
                return VoidResult::Ok();
            }
            if (GetLastError() == ERROR_ALREADY_EXISTS)
            {
                return VoidResult::Ok();
            }
        }

        return _lastError("CreateDirectory", path);
    }

    bool FileExists(std::string_view path) noexcept
    {
        std::wstring const widePath = ToUtf16(path);

        DWORD const attributes = GetFileAttributesW(widePath.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            return false;
        }
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    VoidResult RenameFile(std::string_view from, std::string_view to)
    {
        std::wstring const wideFrom = ToUtf16(from);
        std::wstring const wideTo = ToUtf16(to);

        if (MoveFileExW(wideFrom.c_str(), wideTo.c_str(), MOVEFILE_REPLACE_EXISTING) == 0)
        {
            return _lastError("MoveFileEx (rename)", from);
        }
        return VoidResult::Ok();
    }
}
