#include <catch2/catch_test_macros.hpp>
#include "map/risk/RiskLimits.hpp"
#include "map/core/Event.hpp"
#include "map/Types.hpp"

using namespace map;

TEST_CASE("RiskLimits rejects invalid order quantities", "[risk]")
{
    RiskLimits risk;
    REQUIRE(risk.checkOrder("TEST", Side::Bid, Quantity{1}));
    REQUIRE_FALSE(risk.checkOrder("TEST", Side::Bid, Quantity{0}));
    REQUIRE_FALSE(risk.checkOrder("TEST", Side::Bid, Quantity{-1}));
}

TEST_CASE("RiskLimits enforces max order size", "[risk]")
{
    RiskLimits risk;
    Quantity limit = RiskLimits::maxOrderQty();
    Quantity over_limit = Quantity{limit.raw() + 1};

    REQUIRE(risk.checkOrder("TEST", Side::Bid, limit));
    REQUIRE_FALSE(risk.checkOrder("TEST", Side::Bid, over_limit));
}

TEST_CASE("RiskLimits enforces max position", "[risk]")
{
    RiskLimits risk;
    Quantity max_pos = RiskLimits::maxPositionQty();

    risk.onTrade("TEST", Side::Bid, Quantity{900}, Price{100});
    auto state = risk.getState("TEST");
    REQUIRE(state.netQty.raw() == 900);

    // An order of 100 is accepted, but 101 surpasses max position of 1000
    REQUIRE(risk.checkOrder("TEST", Side::Bid, Quantity{100}));
    REQUIRE_FALSE(risk.checkOrder("TEST", Side::Bid, Quantity{101}));

    risk.onTrade("TEST", Side::Ask, Quantity{500}, Price{100});
    REQUIRE(risk.getState("TEST").netQty.raw() == 400);

    // A sell order of 401 is accepted, bringing position to -1
    REQUIRE(risk.checkOrder("TEST", Side::Ask, Quantity{401}));
}