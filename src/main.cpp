#include <iostream>
#include "map/OrderBook.hpp"
#include "map/Side.hpp"
#include "map/Types.hpp"

int main() {
    map::OrderBook ob;
    ob.addOrder(map::Side::Bid, map::Price{100}, map::Quantity{10});
    ob.addOrder(map::Side::Ask, map::Price{105}, map::Quantity{5});

    std::cout << "map-hft: OrderBook basic demo\n";
    if (auto b = ob.bestBid()) {
        std::cout << "Best bid: " << b->raw() << "\n";
    }
    if (auto a = ob.bestAsk()) {
        std::cout << "Best ask: " << a->raw() << "\n";
    }
    return 0;
}
