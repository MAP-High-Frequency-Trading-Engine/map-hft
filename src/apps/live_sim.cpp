#include <iostream>
#include "map/core/EventBus.hpp"
#include "map/core/Logger.hpp"
#include "map/OrderBook.hpp"
#include "map/Side.hpp"

using namespace map;

int main() {
    EventBus bus;
    Logger   logger("events.bin");
    OrderBook book;

    // Subscribe OrderBook to NewOrderEvent + CancelOrderEvent
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        // For now, we ignore 'symbol' and just use one book
        auto id = book.addOrder(e.side, e.price, e.qty);
        std::cout << "Added order id=" << id.raw()
                  << " side=" << (e.side == Side::Bid ? "Bid" : "Ask")
                  << " px=" << e.price.raw()
                  << " qty=" << e.qty.raw()
                  << "\n";
    });

    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) {
        bool ok = book.cancelOrder(e.id);
        std::cout << "Cancel " << e.id.raw()
                  << (ok ? " OK\n" : " FAILED\n");
    });

    // Subscribe Logger to all 3 event types
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        logger.log(e);
    });
    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) {
        logger.log(e);
    });
    bus.subscribe<TradeEvent>([&](const TradeEvent& e) {
        logger.log(e);
    });

    // --- Simple simulation: publish some events ---

    NewOrderEvent e1{
        .symbol = "TEST",
        .side   = Side::Bid,
        .price  = Price{100},
        .qty    = Quantity{10}
    };

    NewOrderEvent e2{
        .symbol = "TEST",
        .side   = Side::Ask,
        .price  = Price{105},
        .qty    = Quantity{5}
    };

    bus.publish(e1);
    bus.publish(e2);

    std::cout << "Best bid: "
              << (book.bestBid() ? book.bestBid()->raw() : -1)
              << " | Best ask: "
              << (book.bestAsk() ? book.bestAsk()->raw() : -1)
              << "\n";

    return 0;
}
