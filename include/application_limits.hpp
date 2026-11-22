#pragma once

#include "errors.hpp"
#include <cstddef>

namespace ApplicationLimits {
inline constexpr std::size_t maxVertices = 100'000;
inline constexpr std::size_t maxEdges = 2'000'000;
inline constexpr std::size_t maxGeneratedVertices = 2000;
inline constexpr std::size_t maxRepetitionCount = 101;
inline constexpr std::size_t maxSeriesSizeCount = 20;
}

inline Result<void> validateGraphLimits(std::size_t vertexCount, std::size_t edgeCount) {
    if (vertexCount > ApplicationLimits::maxVertices || edgeCount > ApplicationLimits::maxEdges) {
        return failure(ErrorCode::LimitExceeded,
                       "Maximum: " + std::to_string(ApplicationLimits::maxVertices) + " vertices, " +
                           std::to_string(ApplicationLimits::maxEdges) + " edges.");
    }
    return {};
}
