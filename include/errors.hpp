#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

enum class GraphErrorCode {
#define DOR3_ERROR(code, message, hint) code,
#include "error_catalog.def"
#undef DOR3_ERROR
    Count
};

struct GraphErrorDescription {
    std::string_view message;
    std::string_view hint;
};

inline constexpr GraphErrorDescription graphErrorCatalog[] = {
#define DOR3_ERROR(code, message, hint) {message, hint},
#include "error_catalog.def"
#undef DOR3_ERROR
};
static_assert(sizeof(graphErrorCatalog) / sizeof(GraphErrorDescription) ==
              static_cast<unsigned>(GraphErrorCode::Count));

struct GraphError {
    GraphErrorCode code;
    std::string detail;
};
template <class T>
using Result = std::expected<T, GraphError>;

inline auto failure(GraphErrorCode code, std::string detail = {}) {
    return std::unexpected(GraphError{code, std::move(detail)});
}

inline std::string formatError(const GraphError& error) {
    auto index = static_cast<unsigned>(error.code);
    if (index >= static_cast<unsigned>(GraphErrorCode::Count)) {
        index = static_cast<unsigned>(GraphErrorCode::Unexpected);
    }
    const auto& entry = graphErrorCatalog[index];
    return std::string(entry.message) + "\n" + error.detail + "\n" + std::string(entry.hint);
}
