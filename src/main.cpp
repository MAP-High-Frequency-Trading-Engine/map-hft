#include <iostream>
#include "map/OrderBook.hpp"

int main() {
    OrderBook ob;
    ob.addOrder(Side::Bid, Price{100}, Quantity{10});
    ob.addOrder(Side::Ask, Price{105}, Quantity{5});

    std::cout << "map-hft: OrderBook basic demo\n";
    if (auto b = ob.bestBid()) {
        std::cout << "Best bid: " << b->raw() << "\n";
    }
    if (auto a = ob.bestAsk()) {
        std::cout << "Best ask: " << a->raw() << "\n";
    }
    return 0;
}
