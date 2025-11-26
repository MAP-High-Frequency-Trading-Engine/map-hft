#include "map/strategy/BasicStrategy.hpp"

#include "map/core/Event.hpp"
#include "map/OrderBook.hpp"

#include <algorithm>
#include <cmath>

using namespace map;

BasicStrategy::BasicStrategy(EventBus& bus,
                             const std::string& symbol,
                             const Params& p)
    : bus_(bus),
      symbol_(symbol),
      basePrice_(p.basePrice),
      clipSize_(p.clipSize),
      ticksPerOrder_(p.ticksPerOrder),
      aggressivenessMode_(p.aggressivenessMode),
      minClip_(p.minClip),
      maxClip_(p.maxClip),
      minTicksPerOrder_(p.minTicksPerOrder),
      maxTicksPerOrder_(p.maxTicksPerOrder),
      intentRecalcInterval_(p.intentRecalcInterval),
      lpThreshold_(p.lpThreshold),
      ltThreshold_(p.ltThreshold),
      lpBaseOffset_(p.lpBaseOffset),
      currentClip_(p.clipSize),
      currentTicksPerOrder_(p.ticksPerOrder),
      lagInterval_(p.lagInterval),
      rng_(std::mt19937::default_seed),
      uni01_(0.0, 1.0)
{
    // default: use both internal book imbalance and external signal if present
    useBookImbalance_ = true;
}

/**
 * Recompute strategy "intent" based on:
 *   - internal order-book imbalance (Queue-depth) [optional]
 *   - optional externalImbalance_ from real LOB data
 *
 * Result affects:
 *   - currentTicksPerOrder_ (how frequently we fire)
 *   - currentClip_ (size of orders)
 */
void BasicStrategy::updateIntent(const OrderBook& book) {
    // internal imbalance from synthetic book, if enabled
    double lobImb = 0.0;
    if (useBookImbalance_) {
        lobImb = book.orderImbalance(3);  // near top 3 levels
    }

    double imb;
    if (hasExternalImbalance_) {
        if (useBookImbalance_) {
            // Blend internal and external signal 50/50
            imb = 0.5 * lobImb + 0.5 * externalImbalance_;
        } else {
            // Fast mode: external signal only
            imb = externalImbalance_;
        }
    } else {
        // Only internal, or zero if internal disabled
        imb = lobImb;
    }

    lastImbalance_ = imb;

    // aggressivenessMode_ changes sensitivity
    double sensitivity =
        (aggressivenessMode_ == 0 ? 0.5 :
         aggressivenessMode_ == 2 ? 1.5 :
                                    1.0);

    double e   = imb * sensitivity;
    double mag = std::min(1.0, std::abs(e));   // clamp to [0,1]

    // Adaptive firing rate: faster when |imb| is large
    double tt = static_cast<double>(maxTicksPerOrder_) -
                mag * static_cast<double>(maxTicksPerOrder_ - minTicksPerOrder_);

    std::uint64_t ticks =
        std::max<std::uint64_t>(1,
            static_cast<std::uint64_t>(std::llround(tt)));

    currentTicksPerOrder_ = ticks;

    // Adaptive clip size between [minClip_, maxClip_]
    double c = static_cast<double>(minClip_.raw()) +
               mag * static_cast<double>(maxClip_.raw() - minClip_.raw());

    int clipRaw = static_cast<int>(std::llround(c));
    if (clipRaw <= 0) {
        clipRaw = 1;
    }
    currentClip_ = Quantity(static_cast<std::int32_t>(clipRaw));
}

/**
 * Liquidity Providing behaviour:
 *
 * - If imbalance is strongly positive (book is bid-heavy):
 *     Place passive sell (ASK) orders slightly ABOVE mid.
 * - If imbalance is strongly negative (book is ask-heavy):
 *     Place passive buy (BID) orders slightly BELOW mid.
 * - Around neutral imbalance:
 *     Small random jitter around mid, both sides.
 */
void BasicStrategy::fireOne() {
    int midTicks = basePrice_.raw();
    if (midTicks <= 0) midTicks = 1;

    double imb    = lastImbalance_;
    double imbAbs = std::abs(imb);

    Side side;
    int  px = midTicks;

    if (imb > lpThreshold_) {
        // Bid-heavy book → provide liquidity on ASK side a bit above mid
        side = Side::Ask;
        px += lpBaseOffset_ + static_cast<int>(std::floor(imbAbs * 5.0));
    } else if (imb < -lpThreshold_) {
        // Ask-heavy book → provide liquidity on BID side a bit below mid
        side = Side::Bid;
        px -= lpBaseOffset_ + static_cast<int>(std::floor(imbAbs * 5.0));
    } else {
        // Neutral zone: small symmetric jitter around mid
        double u = uni01_(rng_);
        side = (u < 0.5 ? Side::Bid : Side::Ask);
        px += (side == Side::Bid ? -1 : +1);
    }

    if (px <= 0) px = 1;

    NewOrderEvent e{};
    e.symbol = symbol_;
    e.side   = side;
    e.price  = Price(px);
    e.qty    = currentClip_;

    bus_.publish(e);
}

// --- Main tick entrypoint ---
void BasicStrategy::onTick(std::uint64_t t, OrderBook& book) {

    // Recompute intent only every lagInterval_ * intentRecalcInterval_ ticks
    if (t % (lagInterval_ * intentRecalcInterval_) == 0) {
        updateIntent(book);
    }

    if (t > 0 && (t % currentTicksPerOrder_) == 0) {
        fireOne();
    }
}
