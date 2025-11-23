#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "map/Side.hpp"
#include "map/Types.hpp"
#include "map/core/EventBus.hpp"
#include "map/OrderBook.hpp"

namespace map {

class BasicStrategy {
public:
    struct Params {
        // Base configuration (will be overridden by real mid)
        Price         basePrice;
        Quantity      clipSize;
        std::uint64_t ticksPerOrder;

        // Aggressiveness tuning
        int           aggressivenessMode = 1; // 0=slow,1=medium,2=aggressive
        Quantity      minClip           = Quantity(std::int32_t(1));
        Quantity      maxClip           = Quantity(std::int32_t(20));
        std::uint64_t minTicksPerOrder  = 1;
        std::uint64_t maxTicksPerOrder  = 10;

        // How often to recompute intent (to save work)
        std::uint64_t intentRecalcInterval = 2; // every N ticks

        // LP/LT behaviour
        double        lpThreshold      = 0.20;  // imbalance where we start LP bias
        double        ltThreshold      = 0.60;  // imbalance where we might “take”
        int           lpBaseOffset     = 2;     // ticks away from mid for LP quotes
    };

    BasicStrategy(EventBus& bus,
                  const std::string& symbol,
                  const Params& params);

    // Called each simulation tick
    void onTick(std::uint64_t t, OrderBook& book);

    // --- Hooks for external (real) market data ---
    void setMidPrice(Price p) { basePrice_ = p; }
    void setSpread(int s) { externalSpread_ = s; }

    // Use external order-imbalance signal (-1..1)
    void setExternalImbalance(double x) {
        externalImbalance_      = x;
        hasExternalImbalance_   = true;
    }

private:
    void fireOne();                     // send one order based on current intent
    void updateIntent(const OrderBook& book);  // recompute aggressiveness / LP vs LT

    EventBus&     bus_;
    std::string   symbol_;

    // core configuration
    Price         basePrice_;
    Quantity      clipSize_;
    std::uint64_t ticksPerOrder_;

    // adaptive intent parameters
    int           aggressivenessMode_;
    Quantity      minClip_;
    Quantity      maxClip_;
    std::uint64_t minTicksPerOrder_;
    std::uint64_t maxTicksPerOrder_;
    std::uint64_t intentRecalcInterval_;

    // LP/LT configuration
    double        lpThreshold_;
    double        ltThreshold_;
    int           lpBaseOffset_;

    // current state
    Quantity      currentClip_;
    std::uint64_t currentTicksPerOrder_;
    double        lastImbalance_ = 0.0;   // blended signal we’re using

    // external signal fields
    double        externalImbalance_    = 0.0;
    bool          hasExternalImbalance_ = false;
    int           externalSpread_       = 0;

    // randomness
    std::mt19937                    rng_;
    std::uniform_real_distribution<double> uni01_;
};

} // namespace map
