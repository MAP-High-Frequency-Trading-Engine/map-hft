#include "map/strategy/BasicStrategy.hpp"
#include "map/core/Event.hpp"
#include "map/OrderBook.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace map;

// --- Constructor ---
BasicStrategy::BasicStrategy(EventBus& bus,
                             const std::string& symbol,
                             const Params& params)
    : bus_(bus),
      symbol_(symbol),
      basePrice_(params.basePrice),
      clipSize_(params.clipSize),
      ticksPerOrder_(params.ticksPerOrder),
      aggressivenessMode_(params.aggressivenessMode),
      minClip_(params.minClip),
      maxClip_(params.maxClip),
      minTicksPerOrder_(params.minTicksPerOrder),
      maxTicksPerOrder_(params.maxTicksPerOrder),
      intentRecalcInterval_(params.intentRecalcInterval),
      lpThreshold_(params.lpThreshold),
      ltThreshold_(params.ltThreshold),
      lpBaseOffset_(params.lpBaseOffset),
      currentClip_(params.clipSize),
      currentTicksPerOrder_(params.ticksPerOrder),
      rng_(std::mt19937::default_seed),
      uni01_(0.0, 1.0)
{
    // nothing else
}

/**
 * Recompute strategy "intent" based on:
 *   - internal order-book imbalance (Queue-depth)
 *   - optional externalImbalance_ from real LOB data
 *
 * Result affects:
 *   - currentTicksPerOrder_ (how frequently we fire)
 *   - currentClip_ (size of orders)
 *   - lastImbalance_ (direction and strength used by fireOne)
 */
void BasicStrategy::updateIntent(const OrderBook& book) {
    // internal imbalance from our synthetic book ([-1,1])
    double lobImb = book.orderImbalance(3);  // near the top 3 levels

    double imb = lobImb;
    if (hasExternalImbalance_) {
        // Blend internal and external signal 50/50
        imb = 0.5 * lobImb + 0.5 * externalImbalance_;
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
 * "Liquidity Providing / Taking" behaviour:
 *
 * - If imbalance is strongly positive (book is bid-heavy):
 *     Place passive sell (ASK) orders slightly ABOVE mid.
 *     → You provide liquidity to buyers at good prices.
 *
 * - If imbalance is strongly negative (book is ask-heavy):
 *     Place passive buy (BID) orders slightly BELOW mid.
 *
 * - Around neutral imbalance:
 *     Very small random jitter around mid, both sides.
 */
void BasicStrategy::fireOne() {
    // Make sure basePrice_ is sane
    int midTicks = basePrice_.raw();
    if (midTicks <= 0) {
        midTicks = 1;
    }

    double imb     = lastImbalance_;
    double imbAbs  = std::abs(imb);

    Side side;
    int  pxTicks = midTicks;

    // --- Strong positive imbalance → LP on ASK side ---
    if (imb > lpThreshold_) {
        side = Side::Ask;

        // Offset increases with imbalance strength
        int extra = static_cast<int>(std::floor(imbAbs * 5.0));
        int offset = lpBaseOffset_ + extra;
        pxTicks = midTicks + offset;
    }
    // --- Strong negative imbalance → LP on BID side ---
    else if (imb < -lpThreshold_) {
        side = Side::Bid;

        int extra = static_cast<int>(std::floor(imbAbs * 5.0));
        int offset = lpBaseOffset_ + extra;
        pxTicks = midTicks - offset;
    }
    // --- Neutral zone: small symmetric jitter around mid ---
    else {
        double u = uni01_(rng_);
        side = (u < 0.5 ? Side::Bid : Side::Ask);
        int smallOffset = 1;

        if (side == Side::Bid) {
            pxTicks = midTicks - smallOffset;
        } else {
            pxTicks = midTicks + smallOffset;
        }
    }

    // Ensure price stays positive
    if (pxTicks <= 0) {
        pxTicks = 1;
    }

    Price newPrice(static_cast<std::int32_t>(pxTicks));

    NewOrderEvent e{};
    e.symbol = symbol_;
    e.side   = side;
    e.price  = newPrice;
    e.qty    = currentClip_;   // adaptive size

    bus_.publish(e);
}

// --- Main tick entrypoint ---
void BasicStrategy::onTick(std::uint64_t t, OrderBook& book) {
    // Recompute intent only every N ticks to save CPU
    if (t % intentRecalcInterval_ == 0) {
        updateIntent(book);
    }

    // Fire when we hit the adaptive tick interval
    if (t > 0 && (t % currentTicksPerOrder_) == 0) {
        fireOne();
    }
}
