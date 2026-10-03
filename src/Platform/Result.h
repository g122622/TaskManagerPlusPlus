// Common result and error types used across the Platform layer.
//
// The project avoids exceptions for ordinary error flow (see
// docs/CODE_CONVENTIONS.md). Probes return Result<T> instead.
#pragma once

#include <string>
#include <utility>
#include <variant>

namespace tmpp
{
    /**
     * @brief Error codes shared by all probes.
     */
    enum class ErrorCode : int
    {
        None = 0,
        NotFound,
        InvalidArgument,
        NotSupported,
        AccessDenied,
        BufferTooSmall,
        NativeFailure,
    };

    /**
     * @brief An error produced by a probe.
     */
    class Error
    {
    public:
        Error() = default;

        Error(ErrorCode code, std::string message, std::string source)
            : m_code(code), m_message(std::move(message)), m_source(std::move(source))
        {
        }

        [[nodiscard]] ErrorCode Code() const noexcept { return m_code; }
        [[nodiscard]] std::string const& Message() const noexcept { return m_message; }
        [[nodiscard]] std::string const& Source() const noexcept { return m_source; }

    private:
        ErrorCode m_code{ErrorCode::None};
        std::string m_message;
        std::string m_source;
    };

    /**
     * @brief Result of an operation that can fail.
     *
     * Either holds a value of type T or an Error.
     */
    template <typename T>
    class Result
    {
    public:
        Result(T value) : m_storage(std::move(value)) {}
        Result(Error error) : m_storage(std::move(error)) {}

        [[nodiscard]] bool Success() const noexcept { return std::holds_alternative<T>(m_storage); }

        [[nodiscard]] T const& Value() const { return std::get<T>(m_storage); }
        [[nodiscard]] T& Value() { return std::get<T>(m_storage); }

        [[nodiscard]] Error const& GetError() const { return std::get<Error>(m_storage); }

    private:
        std::variant<T, Error> m_storage;
    };

    /**
     * @brief Result type for operations that produce no value.
     */
    class VoidResult
    {
    public:
        VoidResult() = default;
        VoidResult(Error error) : m_error(std::move(error)), m_success(false) {}

        [[nodiscard]] bool Success() const noexcept { return m_success; }
        [[nodiscard]] Error const& GetError() const { return m_error; }

        static VoidResult Ok() { return VoidResult{}; }

    private:
        Error m_error;
        bool m_success{true};
    };
}