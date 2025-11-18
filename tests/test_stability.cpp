#include <catch2/catch_test_macros.hpp>

#include "map/core/EventBus.hpp"
#include "map/core/Event.hpp"
#include "map/OrderBook.hpp"
#include "map/Types.hpp"
#include "map/Side.hpp"

using namespace map;

TEST_CASE("High-volume stability", "[stability]") {
    EventBus bus;
    OrderBook book;
    std::uint64_t N = 10000;

    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        book.addOrder(e.side, e.price, e.qty, e.symbol);
    });

    for (std::uint64_t i = 0; i < N; ++i) {
        NewOrderEvent e;
        e.symbol = "TEST";
        e.side   = (i % 2 == 0 ? Side::Bid : Side::Ask);
        e.price  = Price{100 + static_cast<int>(i % 7)};
        e.qty    = Quantity{1 + static_cast<int>(i % 3)};
        bus.publish(e);
    }

    REQUIRE(book.checksum() != 0);
}
