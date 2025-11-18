#include "map/strategy/BasicStrategy.hpp"
#include "map/core/Event.hpp"
#include "map/OrderBook.hpp"

#include <cmath>    // std::round, std::abs, std::min

using namespace map;

// --- Helper: fire a single order based on current intent state ---
void BasicStrategy::fireOne() {
    // Choose side with bias: ~50–70% on biasSide_, rest opposite
    int r = sideDist_(rng_); // 0 or 1
    Side side = (r == 0 ? biasSide_ : opposite(biasSide_));

    // Price noise: +/- ~5 ticks around base
    int priceTicks = static_cast<int>(std::round(priceNoise_(rng_) * 5.0));

    Price newPrice;
    if (side == Side::Bid) {
        // bids try to buy cheaper
        newPrice = Price{ basePrice_.raw() - priceTicks };
    } else {
        // asks try to sell higher
        newPrice = Price{ basePrice_.raw() + priceTicks };
    }

    // Safety clamp: keep price strictly positive
    if (newPrice.raw() <= 0) {
        newPrice = Price{1};
    }

    // Build and publish event
    NewOrderEvent e{};
    e.symbol = symbol_;
    e.side   = side;
    e.price  = newPrice;
    e.qty    = currentClip_;   // <-- dynamic size driven by intent

    bus_.publish(e);
}

// --- BasicStrategy Lifecycle ---

BasicStrategy::BasicStrategy(EventBus& bus,
                             const std::string& symbol,
                             const Params& params)
    : bus_(bus),
      symbol_(symbol),
      basePrice_(params.basePrice),
      clipSize_(params.clipSize),
      ticksPerOrder_(params.ticksPerOrder),

      // intent / aggressiveness
      aggressivenessMode_(params.aggressivenessMode),
      minClip_(params.minClip),
      maxClip_(params.maxClip),
      minTicksPerOrder_(params.minTicksPerOrder),
      maxTicksPerOrder_(params.maxTicksPerOrder),
      currentClip_(params.clipSize),
      currentTicksPerOrder_(params.ticksPerOrder),
      biasSide_(Side::Bid),

      active_orders_(),
      // deterministic RNG
      rng_(std::mt19937::default_seed),
      sideDist_(0, 1),
      priceNoise_(0.0, 1.0)
{
    // nothing else for now
}

// --- Intent update: read order book and adjust aggression ---
void BasicStrategy::updateIntent(const OrderBook& book) {
    // orderImbalance in [-1, 1]
    double imb = book.orderImbalance(3); // top 3 levels

    // Map aggressivenessMode to sensitivity
    double sensitivity;
    switch (aggressivenessMode_) {
        case 0: sensitivity = 0.5; break;  // slow
        case 2: sensitivity = 1.5; break;  // aggressive
        default: sensitivity = 1.0; break; // medium
    }

    double enhanced = imb * sensitivity;
    double mag = std::min(1.0, std::abs(enhanced)); // [0,1]

    // Bias toward the heavy side
    biasSide_ = (enhanced >= 0.0 ? Side::Bid : Side::Ask);

    // Dynamic ticks-per-order:
    // strong imbalance -> closer to minTicksPerOrder_ (more frequent)
    double t = static_cast<double>(maxTicksPerOrder_) -
               mag * (static_cast<double>(maxTicksPerOrder_) -
                      static_cast<double>(minTicksPerOrder_));
    if (t < 1.0) t = 1.0;
    currentTicksPerOrder_ =
        static_cast<std::uint64_t>(std::round(t));

    // Dynamic clip size:
    double minC = static_cast<double>(minClip_.raw());
    double maxC = static_cast<double>(maxClip_.raw());
    double c = minC + mag * (maxC - minC);

    int cInt = static_cast<int>(std::round(c));
    if (cInt < 1) cInt = 1;
    currentClip_ = Quantity{cInt};
}

/**
 * @brief Called each simulation tick.
 * @param t The current tick number.
 * @param book The current state of the OrderBook.
 */
void BasicStrategy::onTick(std::uint64_t t, OrderBook& book) {
    // 1) Update intent from current order book state
    updateIntent(book);

    // 2) Fire based on dynamic ticks-per-order
    if (t > 0 && (t % currentTicksPerOrder_ == 0)) {
        fireOne();
    }
}
