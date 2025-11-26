#include "map/risk/ImpactModel.hpp"

#include <algorithm>
#include <cmath>

namespace map {

ImpactModel::ImpactModel(const ImpactParams& params)
    : params_(params) {}

void ImpactModel::onNewTick(const std::string& symbol, double mid, double spread) {
    SymState& st = state_[symbol];

    // EWMA of mid changes as a proxy for volatility
    if (st.hasLastMid) {
        double d = mid - st.lastMid;
        double varIncrement = d * d;
        st.ewmaVar = params_.volAlpha * varIncrement
                   + (1.0 - params_.volAlpha) * st.ewmaVar;
    } else {
        st.ewmaVar = 0.0;
        st.hasLastMid = true;
    }

    st.lastMid    = mid;
    st.lastSpread = spread;

    // Exponential decay of temporary impact toward zero
    double decay = std::exp(-1.0 / std::max(1.0, params_.tempHalfLifeTicks));
    st.tempImpact *= decay;
}

double ImpactModel::clampImpact(double impact) const {
    if (impact > params_.maxImpactTicks)  return params_.maxImpactTicks;
    if (impact < -params_.maxImpactTicks) return -params_.maxImpactTicks;
    return impact;
}

double ImpactModel::adverseProbability(const std::string& symbol,
                                       double imbalance) const {
    auto it = state_.find(symbol);
    if (it == state_.end()) {
        return 0.5; // neutral
    }
    const SymState& st = it->second;

    // Volatility proxy (std dev of mid changes)
    double vol = std::sqrt(std::max(0.0, st.ewmaVar));

    // Normalize inputs crudely to [0, 1]
    double imbNorm   = std::clamp(std::abs(imbalance), 0.0, 1.0);
    double spreadNorm = std::clamp(std::abs(st.lastSpread) / 10.0, 0.0, 1.0);
    double volNorm    = std::clamp(vol / 10.0, 0.0, 1.0);

    // Logistic link: P(adverse move | imbalance, spread, vol)
    double x = 1.0 * imbNorm + 0.7 * spreadNorm + 0.5 * volNorm;
    double prob = 1.0 / (1.0 + std::exp(-x));  // in (0,1)
    return prob;
}

void ImpactModel::onTrade(const std::string& symbol,
                          Side side,
                          double qty,
                          double depth,
                          double imbalance) {
    SymState& st = state_[symbol];

    double sizeNorm = (depth > 0.0 ? std::abs(qty) / depth : std::abs(qty));
    sizeNorm = std::clamp(sizeNorm, 0.0, 10.0);  // crude cap

    double prob = adverseProbability(symbol, imbalance);

    // Direction: if we BUY (Bid), adverse move is downward (bad),
    // so execution price is too HIGH vs fair → positive impact.
    // If we SELL (Ask), adverse move is upward (bad),
    // execution price is too LOW vs fair → negative impact.
    double dir = (side == Side::Bid ? +1.0 : -1.0);

    double tempDelta = params_.tempCoeff * sizeNorm * prob * dir;
    double permDelta = params_.permCoeff * sizeNorm * prob * dir;

    st.tempImpact = clampImpact(st.tempImpact + tempDelta);
    st.permImpact = clampImpact(st.permImpact + permDelta);
}

double ImpactModel::effectiveMid(const std::string& symbol, double rawMid) const {
    auto it = state_.find(symbol);
    if (it == state_.end()) {
        return rawMid;
    }
    const SymState& st = it->second;
    double totalImpact = clampImpact(st.tempImpact + st.permImpact);
    return rawMid + totalImpact;
}

double ImpactModel::executionPrice(const std::string& symbol,
                                   Side side,
                                   double rawMid,
                                   double depth) const {
    auto it = state_.find(symbol);
    if (it == state_.end()) {
        // Fallback: cross at mid
        return rawMid;
    }

    const SymState& st = it->second;
    double impactedMid = effectiveMid(symbol, rawMid);

    // Simple: cross at impacted mid ± half-spread
    double halfSpread = 0.5 * st.lastSpread;
    if (side == Side::Bid) {
        return impactedMid + halfSpread;
    } else {
        return impactedMid - halfSpread;
    }
}

} // namespace map
