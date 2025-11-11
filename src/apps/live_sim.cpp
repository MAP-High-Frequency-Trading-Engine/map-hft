#include <iostream>

#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/core/Logger.hpp"
#include "map/OrderBook.hpp"
#include "map/strategy/BasicStrategy.hpp"

using namespace map;

int main() {
    const std::string symbol = "TEST";

    EventBus  bus;
    Logger    logger("events.bin");
    OrderBook book;

    // Wire book to order events
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        book.addOrder(e.side, e.price, e.qty);
    });

    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) {
        book.cancelOrder(e.id);
    });

    // Wire logger to all events
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) { logger.log(e); });
    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) { logger.log(e); });
    bus.subscribe<TradeEvent>([&](const TradeEvent& e) { logger.log(e); });

    // Strategy
    BasicStrategy::Params params;
    params.basePrice     = Price{100};
    params.clipSize      = Quantity{5};
    params.ticksPerOrder = 3;

    BasicStrategy strat(bus, symbol, params);

    // Run a deterministic, fixed-length simulation
    const std::int64_t ticks = 500;

    for (std::int64_t t = 0; t < ticks; ++t) {
        strat.onTick(t, book);
    }

    std::cout << "Live sim finished.\n";
    std::cout << "Best bid: " << (book.bestBid() ? book.bestBid()->raw() : -1) << "\n";
    std::cout << "Best ask: " << (book.bestAsk() ? book.bestAsk()->raw() : -1) << "\n";
    std::cout << "Checksum: " << book.checksum() << "\n";
    std::cout << "Events written to events.bin\n";

    return 0;
}
