//
// Created by Avi Maslow on 11/9/25.
//
#include <iostream>

#include "map/OrderBook.hpp"
#include "map/core/Event.hpp"
#include "map/replay/LogReader.hpp"

using namespace map;

int main(int argc, char** argv) {
    std::string filename = "events.bin";
    if (argc > 1) {
        filename = argv[1];
    }

    LogReader reader(filename);
    if (!reader.good()) {
        std::cerr << "Failed to open log file: " << filename << "\n";
        return 1;
    }

    OrderBook ob;

    while (auto ev = reader.readNext()) {
        std::visit([&](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, NewOrderEvent>) {
                ob.addOrder(e.side, e.price, e.qty);
            } else if constexpr (std::is_same_v<T, CancelOrderEvent>) {
                ob.cancelOrder(e.id);
            } else {
                // TradeEvent: order book already applied fills when orders were added,
                // so there is nothing to do for book state here.
            }
        }, *ev);
    }

    std::cout << "Replay checksum: " << ob.checksum() << "\n";
    return 0;
}
