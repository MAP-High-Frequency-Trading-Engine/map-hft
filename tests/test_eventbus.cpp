#include <catch2/catch_test_macros.hpp>
#include "map/core/EventBus.hpp"
#include "map/core/Event.hpp"
#include "map/Types.hpp"

using namespace map;

TEST_CASE("EventBus routes events", "[eventbus]") {
    EventBus bus;
    int count = 0;

    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        (void)e;
        ++count;
    });

    NewOrderEvent e{};        // default-construct
    e.symbol = "TEST";
    e.side   = Side::Bid;
    e.price  = Price{100};
    e.qty    = Quantity{1};

    bus.publish(e);
    REQUIRE(count == 1);
}
