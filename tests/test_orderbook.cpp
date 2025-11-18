#include <catch2/catch_test_macros.hpp>
#include "map/OrderBook.hpp"
#include "map/Side.hpp"
#include "map/Types.hpp"

using namespace map;

TEST_CASE("OrderBook initial state", "[orderbook]")
{
    OrderBook ob;
    REQUIRE_FALSE(ob.bestBid().has_value());
    REQUIRE_FALSE(ob.bestAsk().has_value());
}

TEST_CASE("OrderBook simple full match", "[orderbook]")
{
    OrderBook ob;
    ob.addOrder(Side::Ask, Price{100}, Quantity{5}, "TEST");
    ob.addOrder(Side::Bid, Price{100}, Quantity{5}, "TEST");

    // Both orders should fill completely
    REQUIRE_FALSE(ob.bestBid().has_value());
    REQUIRE_FALSE(ob.bestAsk().has_value());
}

TEST_CASE("OrderBook partial fill", "[orderbook]")
{
    OrderBook ob;
    ob.addOrder(Side::Ask, Price{100}, Quantity{10}, "TEST"); // Resting order
    ob.addOrder(Side::Bid, Price{100}, Quantity{4}, "TEST");  // Aggressive order

    // The aggressive bid is filled
    REQUIRE_FALSE(ob.bestBid().has_value());

    // The resting ask should be partially filled
    REQUIRE(ob.bestAsk().has_value());
    REQUIRE(ob.bestAsk().value() == Price{100});
}

TEST_CASE("OrderBook no cross (bid < ask)", "[orderbook]")
{
    OrderBook ob;
    ob.addOrder(Side::Bid, Price{99}, Quantity{10}, "TEST");
    ob.addOrder(Side::Ask, Price{101}, Quantity{10}, "TEST");

    REQUIRE(ob.bestBid().has_value());
    REQUIRE(ob.bestBid().value() == Price{99});

    REQUIRE(ob.bestAsk().has_value());
    REQUIRE(ob.bestAsk().value() == Price{101});
}
