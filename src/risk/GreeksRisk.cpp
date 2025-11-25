#include "map/risk/GreeksRisk.hpp"

#include <cmath>
#include <iostream>

namespace map {

GreeksRisk::GreeksRisk(RiskLimits& baseRisk, EventBus* bus, const Limits& limits)
    : base_(baseRisk),
      bus_(bus),
      limits_(limits)
{
}

void GreeksRisk::updateOptionGreeks(const OptionGreeksEvent& ev) {
    latestOptionGreeks_[ev.symbol] = ev;
}

double GreeksRisk::orderDelta(const NewOrderEvent& order) const {
    double signedQty = (order.side == Side::Bid ? 1.0 : -1.0) *
                       static_cast<double>(order.qty.raw());

    auto it = latestOptionGreeks_.find(order.symbol);
    if (it == latestOptionGreeks_.end()) {
        // Treat as underlying (delta = 1)
        return signedQty;
    }
    return it->second.delta * signedQty * limits_.optionMultiplier;
}

double GreeksRisk::orderGamma(const NewOrderEvent& order) const {
    double signedQty = (order.side == Side::Bid ? 1.0 : -1.0) *
                       static_cast<double>(order.qty.raw());

    auto it = latestOptionGreeks_.find(order.symbol);
    if (it == latestOptionGreeks_.end()) return 0.0;
    return it->second.gamma * signedQty * limits_.optionMultiplier;
}

double GreeksRisk::orderTheta(const NewOrderEvent& order) const {
    double signedQty = (order.side == Side::Bid ? 1.0 : -1.0) *
                       static_cast<double>(order.qty.raw());

    auto it = latestOptionGreeks_.find(order.symbol);
    if (it == latestOptionGreeks_.end()) return 0.0;
    return it->second.theta * signedQty * limits_.optionMultiplier;
}

bool GreeksRisk::check(const NewOrderEvent& order) {
    if (!base_.checkOrder(order.symbol, order.side, order.qty)) {
        return false;
    }

    double projDelta = state_.totalDelta + orderDelta(order);
    double projGamma = state_.totalGamma + orderGamma(order);
    double projTheta = state_.totalTheta + orderTheta(order);

    if (std::abs(projDelta) > limits_.maxAbsDelta) {
        std::cout << "[RISK] Reject order: projected delta " << projDelta
                  << " exceeds limit " << limits_.maxAbsDelta << std::endl;
        return false;
    }
    if (std::abs(projGamma) > limits_.maxGamma) {
        std::cout << "[RISK] Reject order: projected gamma " << projGamma
                  << " exceeds limit " << limits_.maxGamma << std::endl;
        return false;
    }
    if (projTheta < limits_.minTheta) {
        std::cout << "[RISK] Reject order: projected theta " << projTheta
                  << " breaches floor " << limits_.minTheta << std::endl;
        return false;
    }

    return true;
}

void GreeksRisk::onFill(const NewOrderEvent& order) {
    state_.totalDelta += orderDelta(order);
    state_.totalGamma += orderGamma(order);
    state_.totalTheta += orderTheta(order);

    base_.onTrade(order.symbol, order.side, order.qty, order.price);
}

void GreeksRisk::maybeTriggerKillSwitch(double vix) {
    if (lastVix_ <= 0.0 || !bus_) return;
    double pctMove = ((vix - lastVix_) / lastVix_) * 100.0;
    if (pctMove >= limits_.vixKillSwitchPct) {
        CancelAllEvent c{};
        c.reason = "VIX spike " + std::to_string(pctMove) + "%";
        bus_->publish(c);

        KillSwitchEvent ks{};
        ks.reason = c.reason;
        bus_->publish(ks);
        std::cout << "[RISK] Kill switch triggered: " << ks.reason << std::endl;
    }
}

void GreeksRisk::onVixUpdate(double vix) {
    maybeTriggerKillSwitch(vix);
    lastVix_ = vix;
}

} // namespace map
