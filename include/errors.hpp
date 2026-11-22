#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

enum class ErrorCode {
#define DOR3_ERROR(code, message, hint) code,
#include "error_catalog.def"
#undef DOR3_ERROR
    Count
};

struct ErrorDescription {
    std::string_view message;
    std::string_view hint;
};

inline constexpr ErrorDescription errorCatalog[] = {
#define DOR3_ERROR(code, message, hint) {message, hint},
#include "error_catalog.def"
#undef DOR3_ERROR
};
static_assert(sizeof(errorCatalog) / sizeof(ErrorDescription) ==
              static_cast<unsigned>(ErrorCode::Count));

struct Error {
    ErrorCode code;
    std::string detail;
};
template <class T>
using Result = std::expected<T, Error>;

inline auto failure(ErrorCode code, std::string detail = {}) {
    return std::unexpected(Error{code, std::move(detail)});
}

inline std::string formatError(const Error& error) {
    auto index = static_cast<unsigned>(error.code);
    if (index >= static_cast<unsigned>(ErrorCode::Count)) {
        index = static_cast<unsigned>(ErrorCode::Unexpected);
    }
    const auto& entry = errorCatalog[index];
    return std::string(entry.message) + "\n" + error.detail + "\n" + std::string(entry.hint);
}
