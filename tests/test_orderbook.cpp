#include <catch2/catch_test_macros.hpp>
#include "map/OrderBook.hpp"
#include "map/Side.hpp"
#include "map/Types.hpp"

using namespace map;

TEST_CASE("OrderBook simple matching", "[orderbook]") {
    OrderBook ob;
    ob.addOrder(Side::Ask, Price{100}, Quantity{5}, "TEST");
    ob.addOrder(Side::Bid, Price{100}, Quantity{5}, "TEST");

    REQUIRE_FALSE(ob.bestBid().has_value());
    REQUIRE_FALSE(ob.bestAsk().has_value());
}
