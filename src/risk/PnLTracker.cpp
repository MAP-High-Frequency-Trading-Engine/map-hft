// src/risk/PnLTracker.cpp
#include "map/risk/PnLTracker.hpp"

#include <algorithm>

namespace map {

void PnLTracker::onFill(const std::string& symbol,
                        Side side,
                        Price price,
                        Quantity qty)
{
    double px  = static_cast<double>(price.raw());
    double q   = static_cast<double>(qty.raw());
    if (q <= 0.0) return;

    // +q for buy, -q for sell
    double signedQty = (side == Side::Bid ? q : -q);

    auto& st = state_[symbol];

    // If position and trade go in same direction → just update avgPrice
    if (st.position == 0.0 || (st.position > 0.0 && signedQty > 0.0) ||
        (st.position < 0.0 && signedQty < 0.0)) {

        double newPos = st.position + signedQty;
        if (newPos == 0.0) {
            // Fully flattened, realizedPnL unchanged here.
            st.position = 0.0;
            st.avgPrice = 0.0;
        } else {
            // Update VWAP
            double oldNotional = st.avgPrice * st.position;
            double newNotional = px * signedQty;
            st.position        = newPos;
            st.avgPrice        = (oldNotional + newNotional) / newPos;
        }
    } else {
        // Trade reduces or flips the position → realize PnL on the closed part
        double remaining = signedQty;
        // Same direction as current position?
        double sameDirPos = (st.position > 0.0 ? st.position : -st.position);

        // Closed quantity is min(|pos|, |trade|)
        double closeQty = std::min(sameDirPos, std::abs(remaining));

        // Realized PnL = (tradePx - avgPx) * closedQty, with sign
        // If we had +pos and we sell (signedQty<0), then:
        //   realized = (sell_px - avgPx) * closeQty
        // If we had -pos and we buy:
        //   realized = (avgPx - buy_px) * closeQty
        double pnlOnClose = 0.0;

        if (st.position > 0.0 && signedQty < 0.0) {
            // Closing a long
            pnlOnClose = (px - st.avgPrice) * closeQty;
        } else if (st.position < 0.0 && signedQty > 0.0) {
            // Closing a short
            pnlOnClose = (st.avgPrice - px) * closeQty;
        }

        st.realizedPnL += pnlOnClose;

        // Update remaining position
        double newPos = st.position + signedQty;
        st.position   = newPos;

        if (st.position == 0.0) {
            st.avgPrice = 0.0;
        } else {
            // If we flipped direction, avgPrice becomes this trade's price
            if ((st.position > 0.0 && signedQty > 0.0) ||
                (st.position < 0.0 && signedQty < 0.0)) {
                st.avgPrice = px;
            }
            // Otherwise, partial close has already updated realizedPnL
            // and remaining position still uses old avgPrice.
        }
    }
}

void PnLTracker::markToMarket(const std::string& symbol, double mid) {
    auto it = state_.find(symbol);
    if (it == state_.end()) return;
    auto& st = it->second;

    // Unrealized PnL = (mid - avgPrice) * position
    // If position < 0 (short), this works too: (mid - avg)*neg → negative if mid > avg.
    double unreal = (mid - st.avgPrice) * st.position;

    // We don't store it permanently; snapshot() computes on demand.
    // But you can modify this if you want to cache it.
    (void)unreal;
}

PnLSnapshot PnLTracker::snapshot(const std::string& symbol) const {
    PnLSnapshot out;
    auto it = state_.find(symbol);
    if (it == state_.end()) {
        return out;
    }
    const auto& st = it->second;

    out.position = st.position;
    out.avgPrice = st.avgPrice;

    // Mark-to-market with the last known mid is not stored here,
    // so we treat unrealizedPnL as (mid - avg)*pos externally.
    // For now we only store realizedPnL here.
    out.realizedPnL = st.realizedPnL;
    // unrealizedPnL and totalPnL will be filled by caller once mid is known.
    return out;
}

} // namespace map
