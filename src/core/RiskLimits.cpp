//
// Created by Avi Maslow on 11/9/25.
//

#pragma once

#include "map/Types.hpp"

namespace map {

    struct RiskLimits {
        // Max size per individual order
        static constexpr Quantity maxOrderQty() {
            return Quantity{500};
        }

        // Sanity limit on price (to catch obviously-bad inputs)
        static constexpr Price maxPrice() {
            return Price{1'000'000};
        }

        static constexpr Price minPrice() {
            return Price{1};
        }
    };

}
