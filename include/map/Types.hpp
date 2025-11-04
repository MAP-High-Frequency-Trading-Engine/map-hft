#pragma once

#include <cstdint>
#include <compare>

template <typename Tag, typename T>
struct Strong {
    T value;
    constexpr explicit Strong(T v = {}) : value(v) {}
    constexpr T raw() const { return value; }
    auto operator<=>(const Strong&) const = default;
};

struct PriceTag {};
struct QuantityTag {};
struct NotionalTag {};
struct OrderIdTag {};

using Price    = Strong<PriceTag, std::int64_t>;
using Quantity = Strong<QuantityTag, std::int64_t>;
using Notional = Strong<NotionalTag, std::int64_t>;
using OrderId  = Strong<OrderIdTag, std::uint64_t>;

enum class Side { Bid, Ask };
