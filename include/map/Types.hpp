#pragma once

#include <cstdint>

namespace map {

    // Base template for strong typedefs.
    // T is the underlying type (e.g., std::uint64_t)
    // Tag is a unique struct used for compile-time differentiation (e.g., OrderIdTag)
    template <typename Tag, typename T>
    struct Strong {
        T value{};

        // FIX: The explicit constructor prevents implicit conversion,
        // which is what caused the first two errors.
        constexpr explicit Strong(T v = {}) : value(v) {}

        // Add implicit conversion operator to get the raw value, if needed
        constexpr T raw() const { return value; }

        // Comparison operators for use in std::map/set
        auto operator<=>(const Strong& other) const = default;
    };

    // --- Type Tags ---
    struct OrderIdTag {};
    struct PriceTag {};
    struct QuantityTag {};
    struct NotionalTag {}; // Added Notional Tag

    // --- Strong Typedefs ---
    using OrderId  = Strong<OrderIdTag, std::uint64_t>;
    using Price    = Strong<PriceTag, std::int32_t>;
    using Quantity = Strong<QuantityTag, std::int32_t>;
    using Notional = Strong<NotionalTag, std::uint64_t>; // Added Notional definition

} // namespace map