#pragma once

#include <cstdint>
#include <compare>

// Generic strong type wrapper (kept in global namespace)
template <typename Tag, typename T>
struct Strong {
    T value;
    constexpr explicit Strong(T v = {}) : value(v) {}
    constexpr T raw() const { return value; }
    auto operator<=>(const Strong&) const = default;
};

namespace map {

    struct PriceTag {};
    struct QuantityTag {};
    struct NotionalTag {};
    struct OrderIdTag {};

    using Price    = Strong<PriceTag, std::int64_t>;
    using Quantity = Strong<QuantityTag, std::int64_t>;
    using Notional = Strong<NotionalTag, std::int64_t>;
    using OrderId  = Strong<OrderIdTag, std::uint64_t>;

} // namespace map
