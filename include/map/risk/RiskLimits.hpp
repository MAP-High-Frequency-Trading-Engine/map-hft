#pragma once

#include "map/Types.hpp"
#include "map/Side.hpp"
#include <unordered_map>
#include <string>

namespace map {

    struct PositionState {
        Quantity netQty{0};
        Notional totalNotional{0};
    };

    struct RiskLimits {
        static constexpr Quantity maxOrderQty() {
            return Quantity{500};
        }

        static constexpr Quantity maxPositionQty() {
            return Quantity{1000};
        }

        static constexpr Price minPrice() {
            return Price{1};
        }

        static constexpr Price maxPrice() {
            return Price{1'000'000};
        }

        bool checkOrder(const std::string& symbol, Side side, Quantity qty);
        void onTrade(const std::string& symbol, Side side, Quantity qty, Price price);
        PositionState getState(const std::string& symbol) const;

    private:
        std::unordered_map<std::string, PositionState> state_;
    };

} // namespace map
