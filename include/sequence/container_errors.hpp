#pragma once

#include <cstddef>
#include <expected>
#include <string>

enum class ContainerErrorCode {
    IndexOutOfRange,
    CapacityExceeded
};

struct ContainerError {
    ContainerErrorCode code;
    std::size_t requested;
    std::size_t size;
};

template <class T>
using ContainerResult = std::expected<T, ContainerError>;

inline auto containerFailure(ContainerErrorCode code, std::size_t requested, std::size_t size) {
    return std::unexpected(ContainerError{code, requested, size});
}

inline std::string formatError(const ContainerError& error) {
    if (error.code == ContainerErrorCode::IndexOutOfRange) {
        return "Index " + std::to_string(error.requested) +
               " is out of range; container size: " + std::to_string(error.size) + ".";
    }
    return "Requested capacity " + std::to_string(error.requested) +
           " exceeds maximum " + std::to_string(error.size) + ".";
}
