// include/map/risk/PnLTracker.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

#include "map/Types.hpp"
#include "map/Side.hpp"

namespace map {

    struct PnLSnapshot {
        double position     = 0.0;  // signed quantity (long > 0, short < 0)
        double avgPrice     = 0.0;  // volume-weighted average entry price
        double realizedPnL  = 0.0;  // closed PnL
        double unrealizedPnL= 0.0;  // mark-to-market
        double totalPnL     = 0.0;  // realized + unrealized
    };

    class PnLTracker {
    public:
        // Assume every order is fully filled immediately at its price.
        void onFill(const std::string& symbol,
                    Side side,
                    Price price,
                    Quantity qty);

        // Re-mark to market using latest mid
        void markToMarket(const std::string& symbol, double mid);

        // Get snapshot for reporting
        PnLSnapshot snapshot(const std::string& symbol) const;

    private:
        struct SymbolState {
            double position    = 0.0;  // signed size
            double avgPrice    = 0.0;  // avg entry price of *open* position
            double realizedPnL = 0.0;
        };

        std::unordered_map<std::string, SymbolState> state_;
    };

} // namespace map
