#pragma once

#include "errors.hpp"
#include <cstddef>

namespace InputLimits {
inline constexpr std::size_t maxVertices = 100'000;
inline constexpr std::size_t maxEdges = 2'000'000;
inline constexpr std::size_t maxGeneratedVertices = 2000;
inline constexpr std::size_t maxRepetitions = 101;
inline constexpr std::size_t maxSeriesPoints = 20;
}

inline Result<void> validateGraphSize(std::size_t vertices, std::size_t edges) {
    if (vertices > InputLimits::maxVertices || edges > InputLimits::maxEdges) {
        return failure(ErrorCode::LimitExceeded,
                       "Maximum: " + std::to_string(InputLimits::maxVertices) + " vertices, " +
                           std::to_string(InputLimits::maxEdges) + " edges.");
    }
    return {};
}
