#pragma once

#include <cstdint>
#include <compare>

namespace map {

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

    using Price    = Strong<PriceTag,   std::int64_t>;
    using Quantity = Strong<QuantityTag,std::int64_t>;
    using Notional = Strong<NotionalTag,std::int64_t>;
    using OrderId  = Strong<OrderIdTag, std::uint64_t>;

    // ---- type-safe helpers ----

    inline Quantity operator+(Quantity a, Quantity b) {
        return Quantity{a.raw() + b.raw()};
    }

    inline Quantity& operator+=(Quantity& a, Quantity b) {
        a = Quantity{a.raw() + b.raw()};
        return a;
    }

    inline Notional operator*(Price p, Quantity q) {
        return Notional{p.raw() * q.raw()};
    }

} // namespace map
