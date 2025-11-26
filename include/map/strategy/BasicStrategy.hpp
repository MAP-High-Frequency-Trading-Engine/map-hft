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
        Price         basePrice;
        Quantity      clipSize;
        std::uint64_t ticksPerOrder;

        int           aggressivenessMode = 1;
        Quantity      minClip           = Quantity(std::int32_t(1));
        Quantity      maxClip           = Quantity(std::int32_t(20));
        std::uint64_t minTicksPerOrder  = 1;
        std::uint64_t maxTicksPerOrder  = 10;

        std::uint64_t intentRecalcInterval = 2;

        double        lpThreshold      = 0.20;  // when |imbalance| > this → directional
        double        ltThreshold      = 0.60;  // (kept for experimentation)
        int           lpBaseOffset     = 2;     // baseline quote offset in ticks

        int           lagInterval      = 1;     // how often we re-evaluate intent

        // Inventory controls (for inventory-based quoting)
        std::int32_t  maxInventory     = 200;   // in "share" units (qty.raw())
    };

    BasicStrategy(EventBus& bus,
                  const std::string& symbol,
                  const Params& params);

    void onTick(std::uint64_t t, OrderBook& book);

    void setMidPrice(Price p) { basePrice_ = p; }
    void setSpread(int s)     { externalSpread_ = s; }

    void setExternalImbalance(double x) {
        externalImbalance_    = x;
        hasExternalImbalance_ = true;
    }

    // Allow turning off book-based imbalance (for speed experiments)
    void setUseBookImbalance(bool x) { useBookImbalance_ = x; }

    // Optional getter for debugging
    double lastImbalance() const { return lastImbalance_; }

private:
    void fireOne();
    void updateIntent(const OrderBook& book);

    EventBus&    bus_;
    std::string  symbol_;

    Price         basePrice_;
    Quantity      clipSize_;
    std::uint64_t ticksPerOrder_;

    int           aggressivenessMode_;
    Quantity      minClip_;
    Quantity      maxClip_;
    std::uint64_t minTicksPerOrder_;
    std::uint64_t maxTicksPerOrder_;
    std::uint64_t intentRecalcInterval_;

    double        lpThreshold_;
    double        ltThreshold_;
    int           lpBaseOffset_;

    Quantity      currentClip_;
    std::uint64_t currentTicksPerOrder_;
    double        lastImbalance_ = 0.0;

    // lag feature
    int           lagInterval_;

    // external data
    double        externalImbalance_    = 0.0;
    bool          hasExternalImbalance_ = false;
    int           externalSpread_       = 0;

    // Inventory model (simple running net qty in "shares")
    std::int32_t  inventory_      = 0;
    std::int32_t  maxInventory_   = 200;

    bool          useBookImbalance_ = true;

    std::mt19937 rng_;
    std::uniform_real_distribution<double> uni01_;
};

} // namespace map
