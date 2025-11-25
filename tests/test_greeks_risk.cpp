#include <catch2/catch_test_macros.hpp>

#include "map/core/EventBus.hpp"
#include "map/core/Event.hpp"
#include "map/risk/GreeksRisk.hpp"

using namespace map;

TEST_CASE("GreeksRisk blocks orders beyond delta limit", "[greeks-risk]") {
    EventBus bus;
    RiskLimits base;
    GreeksRisk::Limits limits;
    limits.maxAbsDelta = 100.0;
    limits.optionMultiplier = 100.0;
    GreeksRisk risk(base, &bus, limits);

    OptionGreeksEvent opt{};
    opt.symbol = "SPX231122C";
    opt.delta  = 0.5; // 50 delta
    risk.updateOptionGreeks(opt);

    NewOrderEvent order{};
    order.symbol = opt.symbol;
    order.side   = Side::Bid;
    order.qty    = Quantity{3}; // 3 * 0.5 * 100 = 150 delta exposure
    order.price  = Price{100};

    REQUIRE_FALSE(risk.check(order));
}

TEST_CASE("GreeksRisk triggers VIX kill switch", "[greeks-risk]") {
    EventBus bus;
    RiskLimits base;
    GreeksRisk::Limits limits;
    limits.vixKillSwitchPct = 1.0; // low threshold for test
    GreeksRisk risk(base, &bus, limits);

    bool killTriggered = false;
    bus.subscribe<KillSwitchEvent>([&](const KillSwitchEvent&) { killTriggered = true; });

    risk.onVixUpdate(20.0); // seed
    risk.onVixUpdate(20.5); // +2.5%

    REQUIRE(killTriggered);
}
