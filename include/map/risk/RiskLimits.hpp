#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

#include "map/Types.hpp"
#include "map/Side.hpp"
#include "map/core/Event.hpp"   // for TradeEvent, etc.

namespace map {

    struct RiskConfig {
        Quantity maxOrderSize;   // e.g. 1'000
        Quantity maxPosition;    // e.g. 5'000 (net)
        Notional maxNotional;    // e.g. 1'000'000
    };

    struct PositionState {
        Quantity netPosition{};  // long > 0, short < 0
        Notional netNotional{};  // total traded notional
    };

    /// Simple stateful risk engine tracking per-symbol exposure.
    /// Not thread-safe; assume single-threaded sim (which you already do).
    class RiskLimits {
    public:
        explicit RiskLimits(RiskConfig cfg);

        /// Check if an order is allowed under current exposure.
        /// Returns true if allowed, false if it should be rejected.
        bool checkOrder(
            const std::string& symbol,
            Side side,
            Price price,
            Quantity qty
        ) const;

        /// Update risk state on a trade fill.
        void onTrade(
            const std::string& symbol,
            Side side,
            Price price,
            Quantity qty
        );

        /// Expose state for debugging / tests.
        const PositionState* findPosition(const std::string& symbol) const;

        const RiskConfig& config() const { return cfg_; }

    private:
        RiskConfig cfg_;
        // key: symbol
        std::unordered_map<std::string, PositionState> positions_;

        Notional computeOrderNotional(Price price, Quantity qty) const;
        PositionState& getOrCreate(const std::string& symbol);
    };

} // namespace map
