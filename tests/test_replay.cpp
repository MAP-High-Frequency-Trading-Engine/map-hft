// Created by Avi Maslow on 11/9/25.
//
#include <catch2/catch_test_macros.hpp>

#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/core/Logger.hpp"
#include "map/OrderBook.hpp"
#include "map/replay/LogReader.hpp"
#include "map/Types.hpp"

using namespace map;

TEST_CASE("Record + replay produce same book checksum", "[replay]") {
    const std::string symbol = "TEST";
    const std::string fname  = "test_events.bin";

    std::size_t liveChecksum   = 0;
    std::size_t replayChecksum = 0;

    // -----------------------------
    // 1) LIVE PATH: write to file
    // -----------------------------
    {
        EventBus  bus;
        Logger    logger(fname);
        OrderBook liveBook;

        // book subscriptions
        bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
            liveBook.addOrder(e.side, e.price, e.qty);
        });
        bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) {
            liveBook.cancelOrder(e.id);
        });

        // logger subscriptions
        bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) { logger.log(e); });
        bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) { logger.log(e); });
        bus.subscribe<TradeEvent>([&](const TradeEvent& e) { logger.log(e); });

        // deterministic sequence of orders
        for (int i = 0; i < 20; ++i) {
            NewOrderEvent e;
            e.symbol = symbol;
            e.side   = (i % 2 == 0) ? Side::Bid : Side::Ask;
            e.price  = Price(static_cast<std::int32_t>(100 + (i % 5)));

            e.qty    = Quantity{1 + (i % 3)};
            bus.publish(e);
        }

        liveChecksum = liveBook.checksum();
        INFO("liveChecksum = " << liveChecksum);
        // logger destructor runs here, flushing/closing fname
    }

    // -----------------------------
    // 2) REPLAY PATH: read + apply
    // -----------------------------
    {
        LogReader reader(fname);
        REQUIRE(reader.good());

        OrderBook replayBook;
        std::size_t replayedCount = 0;

        while (auto ev = reader.readNext()) {
            ++replayedCount;

            std::visit([&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, NewOrderEvent>) {
                    replayBook.addOrder(e.side, e.price, e.qty);
                } else if constexpr (std::is_same_v<T, CancelOrderEvent>) {
                    replayBook.cancelOrder(e.id);
                } else if constexpr (std::is_same_v<T, TradeEvent>) {
                    // For now we don't change book state for trades in this test,
                    // but we still prove we can decode them.
                }
            }, *ev);
        }

        INFO("replayedCount = " << replayedCount);
        REQUIRE(replayedCount > 0); // sanity check: we actually replayed something

        replayChecksum = replayBook.checksum();
        INFO("replayChecksum = " << replayChecksum);
    }

    REQUIRE(liveChecksum == replayChecksum);
}
