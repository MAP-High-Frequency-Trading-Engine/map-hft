// RiskLimits.hpp - Defines the risk configuration, state structure, and the RiskLimits class.
#pragma once

#include "map/Types.hpp"
#include "map/Side.hpp"
#include <string>
#include <unordered_map>

namespace map {

    // Global Risk Configuration (passed in, immutable)
    struct RiskConfig {
        Notional maxNotional{Notional{1000000}}; // Max total notional exposure (sum of |P*Q|)
        Quantity maxPosition{Quantity{1000}};    // Max total position (net qty)
    };

    // Current per-symbol position state (managed internally by RiskLimits)
    struct PositionState {
        // Positive = Long, Negative = Short.
        Quantity netPosition{Quantity{0}};
        // Notional is tracked for maximum exposure check
        Notional netNotional{Notional{0}};
    };

    class RiskLimits {
    public:
        explicit RiskLimits(const RiskConfig& config) : config_(config) {}

        // --- Core Risk Checks ---

        // Checks if an order can be placed without violating limits (simulates change internally)
        bool checkOrder(const std::string& symbol, Side side, Price price, Quantity qty);

        // --- Risk State Updates ---

        // Updates position and notional based on an executed trade
        void onTrade(const std::string& symbol, Side side, Price price, Quantity qty);

        // --- Accessors for Visualization/Reporting ---

        // Compute the absolute notional value of a single order
        Notional computeOrderNotional(Price price, Quantity qty) const;

        const PositionState& getPositionState(const std::string& symbol) const;
        const RiskConfig& getConfig() const { return config_; }

        // Max size per individual order
        static constexpr Quantity maxOrderQty() { return Quantity{500}; }
        // Very loose price sanity bounds
        static constexpr Price minPrice() { return Price{1}; }
        static constexpr Price maxPrice() { return Price{1'000'000}; }

    private:
        RiskConfig config_;
        std::unordered_map<std::string, PositionState> positionState_;

        // Helper to get or create state
        PositionState& getOrCreateState(const std::string& symbol);
    };

} // namespace map