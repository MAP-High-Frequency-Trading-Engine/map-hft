#include "map/strategy/BasicStrategy.hpp"
#include "map/OrderBook.hpp"

namespace map {

    BasicStrategy::BasicStrategy(EventBus& bus,
                                 const std::string& symbol,
                                 const Params& p)
        : bus_(bus)
        , symbol_(symbol)
        , basePrice_(p.basePrice)
        , clipSize_(p.clipSize)
        , ticksPerOrder_(p.ticksPerOrder == 0 ? 1 : p.ticksPerOrder)
        , rng_(std::random_device{}())
        , sideDist_(0, 1)
        , priceNoise_(0.0, 0.5)   // small noise around base price
    {
    }

    void BasicStrategy::fireOne() {
        bool isBid = (sideDist_(rng_) == 0);
        Side side  = isBid ? Side::Bid : Side::Ask;

        double px = static_cast<double>(basePrice_.raw()) + priceNoise_(rng_);
        if (px < 1.0) px = 1.0;

        NewOrderEvent e;
        e.symbol = symbol_;
        e.side   = side;
        e.price  = Price{static_cast<std::int64_t>(px)};
        e.qty    = clipSize_;

        bus_.publish(e);
    }

    void BasicStrategy::onTick(std::uint64_t t, OrderBook& book) {
        (void)book; // we can use the book later to make this smarter

        // Only place an order every N ticks
        if (t % ticksPerOrder_ != 0) {
            return;
        }

        fireOne();
    }

} // namespace map
