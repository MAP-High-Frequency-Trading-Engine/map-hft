#include <iostream>
#include <string>
#include <cstdint>
#include <chrono>
#include <fstream>

#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/core/Logger.hpp"
#include "map/OrderBook.hpp"
#include "map/strategy/BasicStrategy.hpp"
// #include "map/risk/RiskLimits.hpp"   // keep commented out

using namespace map;

int main(int argc, char** argv) {
    const std::string symbol = "TEST";

    // -------------------------------------------------
    // Benchmark flag: ./live_sim --benchmark
    // -------------------------------------------------
    bool benchmark = (argc > 1 && std::string(argv[1]) == "--benchmark");

    // Count how many NewOrderEvent we actually send into the book
    std::uint64_t newOrders = 0;

    // Optional CSV for performance logging
    std::ofstream perfOut;
    if (benchmark) {
        perfOut.open("perf_live.csv");
        perfOut << "tick,orders\n";
    }

    // -------------------------------------------------
    // Core engine wiring
    // -------------------------------------------------
    EventBus bus;
    Logger   logger("events.bin");
    OrderBook book;

    // Book subscriptions
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        book.addOrder(e.side, e.price, e.qty, e.symbol);
        ++newOrders;
    });

    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) {
        book.cancelOrder(e.id);
    });

    // Logger subscriptions
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) { logger.log(e); });
    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) { logger.log(e); });
    bus.subscribe<TradeEvent>([&](const TradeEvent& e) { logger.log(e); });

    // -------------------------------------------------
    // Strategy
    // -------------------------------------------------
    BasicStrategy::Params params;
    params.basePrice         = Price{100};
    params.clipSize          = Quantity{5};
    params.ticksPerOrder     = 3;
    params.aggressivenessMode = 1;               // 0=slow, 1=medium, 2=aggressive
    params.minClip           = Quantity{1};
    params.maxClip           = Quantity{20};
    params.minTicksPerOrder  = 1;
    params.maxTicksPerOrder  = 10;

    BasicStrategy strat(bus, symbol, params);

    // -------------------------------------------------
    // Run deterministic simulation
    // -------------------------------------------------
    const std::int64_t ticks = 500;

    auto start = std::chrono::steady_clock::now();

    for (std::int64_t t = 0; t < ticks; ++t) {
        strat.onTick(static_cast<std::uint64_t>(t), book);

        if (benchmark && (t % 10 == 0)) {
            perfOut << t << "," << newOrders << "\n";
        }
    }

    auto end  = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(end - start).count();
    double eps  = (secs > 0.0 ? static_cast<double>(newOrders) / secs : 0.0);

    // -------------------------------------------------
    // Benchmark summary
    // -------------------------------------------------
if (benchmark) {
    std::cout << "Benchmark mode\n";
    std::cout << "  Orders generated: " << newOrders << "\n";
    std::cout << "  Elapsed seconds:  " << secs << "\n";
    std::cout << "  Orders/sec:       " << eps << "\n";
    if (newOrders > 0) {
        double latency_ns = (secs * 1e9) / static_cast<double>(newOrders);
        std::cout << "  Latency per event (ns): " << latency_ns << "\n";
    }
}


    // -------------------------------------------------
    // Final state summary
    // -------------------------------------------------
    std::cout << "Live sim finished.\n";
    std::cout << "Best bid: " << (book.bestBid() ? book.bestBid()->raw() : -1) << "\n";
    std::cout << "Best ask: " << (book.bestAsk() ? book.bestAsk()->raw() : -1) << "\n";
    std::cout << "Checksum: " << book.checksum() << "\n";
    std::cout << "Events written to events.bin\n";

    return 0;
}
