#include <cassert>
#include <iostream>

#include "map/OrderBook.hpp"
#include "map/Side.hpp"
#include "map/Types.hpp"

using namespace map;

int main() {
    {
        // Scenario 1: No-cross add (bid < ask)
        OrderBook ob;
        ob.addOrder(Side::Bid, Price{100}, Quantity{10});
        ob.addOrder(Side::Ask, Price{105}, Quantity{5});

        assert(ob.bestBid().has_value());
        assert(ob.bestAsk().has_value());
        assert(ob.bestBid()->raw() == 100);
        assert(ob.bestAsk()->raw() == 105);
    }

    {
        // Scenario 2: Simple full match (bid == ask)
        OrderBook ob;
        ob.addOrder(Side::Ask, Price{100}, Quantity{5});
        ob.addOrder(Side::Bid, Price{100}, Quantity{5});

        // Book should be empty on both sides
        assert(!ob.bestBid().has_value());
        assert(!ob.bestAsk().has_value());
    }

    {
        // Scenario 3: Partial fill
        OrderBook ob;
        ob.addOrder(Side::Ask, Price{100}, Quantity{10});  // resting 10 @ 100
        ob.addOrder(Side::Bid, Price{100}, Quantity{4});   // eats 4

        // Ask should still be present at 100
        assert(ob.bestAsk().has_value());
        assert(ob.bestAsk()->raw() == 100);
    }

    std::cout << "OrderBook smoke tests passed\n";
    return 0;
}
