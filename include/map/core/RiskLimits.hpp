#pragma once

#include "map/Types.hpp"

namespace map {

    // Very simple global risk limits for now
    struct RiskLimits {
        // Max size per individual order
        static constexpr Quantity maxOrderQty() {
            return Quantity{500};
        }

        // Very loose price sanity bounds
        static constexpr Price minPrice() {
            return Price{1};
        }

        static constexpr Price maxPrice() {
            return Price{1'000'000};
        }
    };

} // namespace map
