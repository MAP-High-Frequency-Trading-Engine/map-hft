#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

#include "map/Side.hpp"
#include "map/Types.hpp"
#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"

namespace map::algo {

struct AlgoParams {
    double pegOffset{0.0};        // price improvement vs BBO
    int    chaseIntervalMs{100};  // reprice cadence
    int    maxChaseMs{1000};      // after this, optionally cross
    double minPriceMove{0.01};    // jitter gate
    bool   allowCross{false};     // permit crossing after maxChaseMs
};

struct WorkingOrder {
    std::string symbol;
    Side        side{Side::Bid};
    Quantity    qty;
    double      lastLimit{0.0};
    std::chrono::steady_clock::time_point created;
    std::chrono::steady_clock::time_point lastUpdate;
    bool        crossed{false};
};

// Converts hedge intents into pegged limit orders with chase logic.
class AlgoExecutor {
public:
    AlgoExecutor(EventBus& bus, const AlgoParams& params);

    void onMarketData(const MarketDataEvent& md);

    // Strategy calls this instead of sending market orders directly.
    void onHedgeIntent(const std::string& symbol, Side side, int qty);

    // Pump from main loop with wall-clock ms to reprice/cross.
    void onTick(std::uint64_t nowMs);

private:
    EventBus& bus_;
    AlgoParams params_;
    std::unordered_map<std::string, WorkingOrder> working_;
    std::unordered_map<std::string, MarketDataEvent> md_;

    std::optional<double> calcPegPrice(const WorkingOrder& wo) const;
    void sendLimit(const WorkingOrder& wo, double px, const std::string& cid);
    void cancelAll(const std::string& symbol);
};

} // namespace map::algo
