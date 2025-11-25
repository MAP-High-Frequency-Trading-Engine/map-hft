#include "map/algo/AlgoExecutor.hpp"

#include <cmath>
#include <iostream>

using namespace map;
using namespace map::algo;

AlgoExecutor::AlgoExecutor(EventBus& bus, const AlgoParams& params)
    : bus_(bus),
      params_(params) {}

void AlgoExecutor::onMarketData(const MarketDataEvent& md) {
    md_[md.symbol] = md;
}

std::optional<double> AlgoExecutor::calcPegPrice(const WorkingOrder& wo) const {
    auto it = md_.find(wo.symbol);
    if (it == md_.end()) return std::nullopt;
    const auto& m = it->second;
    if (m.bid <= 0.0 || m.ask <= 0.0) return std::nullopt;

    double px = (wo.side == Side::Bid)
                    ? m.bid + params_.pegOffset
                    : m.ask - params_.pegOffset;

    if (wo.lastLimit > 0.0 && std::abs(px - wo.lastLimit) < params_.minPriceMove) {
        return std::nullopt;
    }
    return px;
}

void AlgoExecutor::sendLimit(const WorkingOrder& wo, double px, const std::string& cid) {
    NewOrderEvent ord{};
    ord.symbol = wo.symbol;
    ord.side   = wo.side;
    ord.qty    = wo.qty;
    ord.type   = OrderType::Limit;
    ord.price  = Price{px};
    ord.tif    = TimeInForce::Day;
    ord.clientOrderId = cid;
    bus_.publish(ord);
}

void AlgoExecutor::cancelAll(const std::string& symbol) {
    CancelAllEvent c{};
    c.reason = "algo-reprice " + symbol;
    bus_.publish(c);
}

void AlgoExecutor::onHedgeIntent(const std::string& symbol, Side side, int qty) {
    WorkingOrder wo;
    wo.symbol = symbol;
    wo.side   = side;
    wo.qty    = Quantity{qty};
    wo.created = wo.lastUpdate = std::chrono::steady_clock::now();
    working_[symbol] = wo;
}

void AlgoExecutor::onTick(std::uint64_t /*nowMs*/) {
    auto now = std::chrono::steady_clock::now();

    for (auto it = working_.begin(); it != working_.end(); ) {
        auto& wo = it->second;
        auto peg = calcPegPrice(wo);
        if (!peg) { ++it; continue; }

        auto ageMs   = std::chrono::duration_cast<std::chrono::milliseconds>(now - wo.created).count();
        auto sinceMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - wo.lastUpdate).count();
        if (sinceMs < params_.chaseIntervalMs) { ++it; continue; }

        double px = *peg;
        bool cross = params_.allowCross && ageMs > params_.maxChaseMs;
        if (cross) {
            auto mdIt = md_.find(wo.symbol);
            if (mdIt != md_.end()) {
                const auto& m = mdIt->second;
                px = (wo.side == Side::Bid) ? m.ask : m.bid;
            }
            wo.crossed = true;
        }

        cancelAll(wo.symbol); // crude replace; assumes only hedge orders live
        sendLimit(wo, px, "algo-peg");
        wo.lastLimit  = px;
        wo.lastUpdate = now;

        if (wo.crossed) {
            it = working_.erase(it);
        } else {
            ++it;
        }
    }
}
