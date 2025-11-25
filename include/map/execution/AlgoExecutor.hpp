#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

#include "map/Side.hpp"
#include "map/Types.hpp"
#include "map/alpaca/AlpacaClient.hpp"
#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"

namespace map::execution {

struct AlgoParams {
    double pegOffset{0.0};
    int    chaseIntervalMs{100};
    int    maxChaseMs{1000};
    double minPriceMove{0.01};
};

struct OrderIntent {
    std::string intentId;
    std::string symbol;
    Side        side{Side::Bid};
    int         qty{0};
    bool        allowCross{false};
};

enum class OrderState {
    New,
    Working,
    Filled,
    Canceled,
    Rejected,
    Done
};

struct WorkingOrder {
    std::string intentId;
    std::string currentAlpacaOrderId;
    std::string symbol;
    Side        side{Side::Bid};
    int         qty{0};
    double      lastLimitPrice{0.0};
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point lastUpdate;
    bool        isCrossingAllowed{false};
    OrderState  state{OrderState::New};
};

class AlgoExecutor {
public:
    AlgoExecutor(EventBus& bus, const alpaca::Credentials& creds, const AlgoParams& params);

    void onMarketData(const MarketDataEvent& md);
    void submitIntent(const OrderIntent& intent);
    bool hasActiveIntent(const std::string& symbol, Side side) const;
    void onOrderAck(const OrderAckEvent& ack);
    void onTick(std::uint64_t nowMs);

private:
    EventBus& bus_;
    alpaca::HttpClient client_;
    AlgoParams params_;
    std::unordered_map<std::string, WorkingOrder> working_;
    std::unordered_map<std::string, MarketDataEvent> md_;

    std::optional<double> calcPegPrice(const WorkingOrder& wo) const;
    std::string sendLimit(const WorkingOrder& wo, double px, const std::string& tif);
    bool replaceOrder(const WorkingOrder& wo, double newPx);
    void cancelOrder(const std::string& orderId);
    std::chrono::steady_clock::time_point now() const { return std::chrono::steady_clock::now(); }
};

} // namespace map::execution
