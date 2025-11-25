#include "map/execution/AlgoExecutor.hpp"

#include <cmath>
#include <iostream>

using namespace map;
using namespace map::execution;

namespace {
std::string makeClientId(const std::string& base) {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now().time_since_epoch()).count();
    return base + "-" + std::to_string(ns);
}
} // namespace

AlgoExecutor::AlgoExecutor(EventBus& bus, const alpaca::Credentials& creds, const AlgoParams& params)
    : bus_(bus),
      client_(creds),
      params_(params) {}

void AlgoExecutor::onMarketData(const MarketDataEvent& md) {
    md_[md.symbol] = md;
}

void AlgoExecutor::submitIntent(const OrderIntent& intent) {
    WorkingOrder wo;
    wo.intentId = intent.intentId;
    wo.symbol   = intent.symbol;
    wo.side     = intent.side;
    wo.qty      = intent.qty;
    wo.isCrossingAllowed = intent.allowCross;
    wo.start = wo.lastUpdate = now();
    working_[wo.intentId] = wo;
}

bool AlgoExecutor::hasActiveIntent(const std::string& symbol, Side side) const {
    for (const auto& [id, wo] : working_) {
        if (wo.symbol == symbol &&
            wo.side == side &&
            (wo.state == OrderState::New || wo.state == OrderState::Working)) {
            return true;
        }
    }
    return false;
}

void AlgoExecutor::onOrderAck(const OrderAckEvent& ack) {
    for (auto& [id, wo] : working_) {
        if (wo.currentAlpacaOrderId == ack.brokerOrderId) {
            wo.state = ack.accepted ? OrderState::Filled : OrderState::Rejected;
            break;
        }
    }
}

std::optional<double> AlgoExecutor::calcPegPrice(const WorkingOrder& wo) const {
    auto it = md_.find(wo.symbol);
    if (it == md_.end()) return std::nullopt;
    const auto& m = it->second;
    if (m.bid <= 0.0 || m.ask <= 0.0) return std::nullopt;

    double px = (wo.side == Side::Bid)
                    ? m.bid + params_.pegOffset
                    : m.ask - params_.pegOffset;

    if (wo.lastLimitPrice > 0.0 && std::abs(px - wo.lastLimitPrice) < params_.minPriceMove) {
        return std::nullopt;
    }
    return px;
}

std::string AlgoExecutor::sendLimit(const WorkingOrder& wo, double px, const std::string& tif) {
    NewOrderEvent ord{};
    ord.symbol = wo.symbol;
    ord.side   = wo.side;
    ord.qty    = Quantity{wo.qty};
    ord.type   = OrderType::Limit;
    ord.price  = Price{px};
    ord.tif    = (tif == "ioc") ? TimeInForce::IOC : TimeInForce::Day;
    ord.clientOrderId = makeClientId(wo.intentId);

    std::ostringstream body;
    body << "{";
    body << "\"symbol\":\"" << ord.symbol << "\",";
    body << "\"side\":\"" << (ord.side == Side::Bid ? "buy" : "sell") << "\",";
    body << "\"type\":\"limit\",";
    body << "\"time_in_force\":\"" << (tif == "ioc" ? "ioc" : "day") << "\",";
    body << "\"client_order_id\":\"" << ord.clientOrderId << "\",";
    body << "\"limit_price\":" << px << ",";
    body << "\"qty\":" << ord.qty.raw();
    body << "}";

    auto resp = client_.postJson("/orders", body.str());
    if (resp.status >= 200 && resp.status < 300) {
        return alpaca::extractString(resp.body, "id");
    }
    std::cerr << "[AlgoExecutor] order submit failed status=" << resp.status
              << " body=" << resp.body << std::endl;
    return {};
}

bool AlgoExecutor::replaceOrder(const WorkingOrder& wo, double newPx) {
    if (wo.currentAlpacaOrderId.empty()) return false;

    std::ostringstream body;
    body << "{";
    body << "\"limit_price\":" << newPx;
    body << "}";

    auto resp = client_.patchJson("/orders/" + wo.currentAlpacaOrderId, body.str());
    if (resp.status >= 200 && resp.status < 300) {
        return true;
    }
    std::cerr << "[AlgoExecutor] replace failed status=" << resp.status
              << " body=" << resp.body << " (will fallback cancel+new)" << std::endl;
    return false;
}

void AlgoExecutor::cancelOrder(const std::string& orderId) {
    if (orderId.empty()) return;
    client_.deletePath("/orders/" + orderId);
}

void AlgoExecutor::onTick(std::uint64_t /*nowMs*/) {
    auto tnow = now();

    for (auto it = working_.begin(); it != working_.end(); ) {
        auto& wo = it->second;

        if (wo.state == OrderState::Filled || wo.state == OrderState::Rejected || wo.state == OrderState::Canceled) {
            wo.state = OrderState::Done;
            it = working_.erase(it);
            continue;
        }

        auto peg = calcPegPrice(wo);
        if (!peg) { ++it; continue; }

        auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(tnow - wo.start).count();
        auto sinceMs = std::chrono::duration_cast<std::chrono::milliseconds>(tnow - wo.lastUpdate).count();

        if (wo.state == OrderState::New) {
            std::string oid = sendLimit(wo, *peg, "day");
            wo.currentAlpacaOrderId = oid;
            wo.lastLimitPrice = *peg;
            wo.lastUpdate = tnow;
            wo.state = OrderState::Working;
            ++it;
            continue;
        }

        if (ageMs > params_.maxChaseMs && wo.isCrossingAllowed) {
            cancelOrder(wo.currentAlpacaOrderId);
            auto mdIt = md_.find(wo.symbol);
            if (mdIt != md_.end()) {
                const auto& m = mdIt->second;
                double crossPx = (wo.side == Side::Bid) ? m.ask : m.bid;
                wo.currentAlpacaOrderId = sendLimit(wo, crossPx, "ioc");
            }
            wo.state = OrderState::Done;
            it = working_.erase(it);
            continue;
        }

        if (sinceMs > params_.chaseIntervalMs && std::abs(*peg - wo.lastLimitPrice) > params_.minPriceMove) {
            if (!replaceOrder(wo, *peg)) {
                cancelOrder(wo.currentAlpacaOrderId);
                wo.currentAlpacaOrderId = sendLimit(wo, *peg, "day");
            }
            wo.lastLimitPrice = *peg;
            wo.lastUpdate = tnow;
        }
        ++it;
    }
}
