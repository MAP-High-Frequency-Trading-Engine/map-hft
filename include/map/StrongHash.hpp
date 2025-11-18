#pragma once

#include "map/Types.hpp" // Assuming this defines the 'Strong' template
#include <functional>
#include <utility>

namespace std {
    /**
     * @brief Specialization of std::hash for the ::map::Strong template.
     * @tparam Tag The Strong type's tag (e.g., OrderIdTag).
     * @tparam T The underlying integer type (e.g., unsigned long long).
     */
    template <typename Tag, typename T>
    struct hash<::map::Strong<Tag, T>> {
        std::size_t operator()(const ::map::Strong<Tag, T>& s) const noexcept {
            return std::hash<T>{}(s.raw());
        }
    };
} // namespace std

