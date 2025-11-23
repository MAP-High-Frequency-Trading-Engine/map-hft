// src/apps/live_sim.cpp
#include <iostream>
#include <string>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <vector>
#include <algorithm>

#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/core/Logger.hpp"
#include "map/OrderBook.hpp"
#include "map/strategy/BasicStrategy.hpp"
#include "map/market/RealLOBFeed.hpp"
#include "map/risk/PnLTracker.hpp"

using namespace map;

struct SymbolContext {
    std::string   symbol;
    RealLOBFeed   feed;
    BasicStrategy* strat = nullptr;  // non-owning
};

int main(int argc, char** argv) {
    // CLI: --freq=1sec|1min|5min, --benchmark
    bool benchmark = false;
    std::string freq = "1sec";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--benchmark") {
            benchmark = true;
        } else if (arg.rfind("--freq=", 0) == 0) {
            freq = arg.substr(std::string("--freq=").size());
        }
    }

    std::vector<std::string> symbols = {"BTC", "ETH", "ADA"};

    std::cout << "Live sim: multi-symbol (BTC, ETH, ADA) freq=" << freq << "\n";

    // -------------------------------------------------
    // Load feeds per symbol
    // -------------------------------------------------
    std::vector<SymbolContext> ctxs;
    ctxs.reserve(symbols.size());

    std::size_t minSize = std::numeric_limits<std::size_t>::max();

    for (const auto& sym : symbols) {
        SymbolContext ctx;
        ctx.symbol = sym;
        std::string path = "data/" + sym + "_" + freq + ".csv";
        std::cout << "  Using " << path << " for " << sym << "\n";

        if (!ctx.feed.loadCSV(path)) {
            std::cerr << "ERROR: failed to load " << path << " for " << sym << "\n";
            return 1;
        }
        minSize = std::min(minSize, ctx.feed.size());

        ctxs.push_back(std::move(ctx));
    }

    // -------------------------------------------------
    // Engine wiring
    // -------------------------------------------------
    EventBus   bus;
    OrderBook  book;
    PnLTracker pnl;

#ifndef MAP_BENCHMARK_MODE
    Logger logger("events.bin");
    // Log all events if not in pure benchmark mode
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) { logger.log(e); });
    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) { logger.log(e); });
    bus.subscribe<TradeEvent>([&](const TradeEvent& e) { logger.log(e); });
#endif

    std::uint64_t newOrders = 0;

    // Book subscriptions (also update PnL here)
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        book.addOrder(e.side, e.price, e.qty, e.symbol);
        ++newOrders;

        // Stylized assumption: every order is a filled trade
        pnl.onFill(e.symbol, e.side, e.price, e.qty);
    });

    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) {
        book.cancelOrder(e.id);
    });

    // -------------------------------------------------
    // Create strategies per symbol
    // -------------------------------------------------
    std::vector<BasicStrategy> strategies;
    strategies.reserve(ctxs.size());

    for (auto& ctx : ctxs) {
        BasicStrategy::Params params;
        params.basePrice          = Price{100};  // overwritten by real mid
        params.clipSize           = Quantity{5};
        params.ticksPerOrder      = 3;
        params.aggressivenessMode = 1;
        params.minClip            = Quantity{1};
        params.maxClip            = Quantity{20};
        params.minTicksPerOrder   = 1;
        params.maxTicksPerOrder   = 10;

        strategies.emplace_back(bus, ctx.symbol, params);
        ctx.strat = &strategies.back();
    }

    // -------------------------------------------------
    // CSV outputs
    // -------------------------------------------------
    std::ofstream perfOut;
    std::ofstream bookStats;
    std::ofstream pnlStats;

    if (benchmark) {
        perfOut.open("perf_live.csv");
        perfOut << "tick,orders\n";

        bookStats.open("book_stats.csv");
        bookStats << "tick,symbol,real_mid,real_spread,real_bidDepth,real_askDepth,"
                  << "engine_bestBid,engine_bestAsk\n";

        pnlStats.open("pnl_stats.csv");
        pnlStats << "tick,symbol,position,avg_price,realized,unrealized,total,mid\n";
    }

    // -------------------------------------------------
    // Run sim
    // -------------------------------------------------
    std::int64_t ticks = static_cast<std::int64_t>(minSize);

    auto start = std::chrono::steady_clock::now();

    for (std::int64_t t = 0; t < ticks; ++t) {
        // For each symbol: update strategy + log book / PnL
        for (auto& ctx : ctxs) {
            const RealSnapshot& snap = ctx.feed.at(static_cast<std::size_t>(t));

            // Drive strategy
            ctx.strat->setMidPrice(Price(static_cast<std::int32_t>(snap.mid)));

            ctx.strat->setSpread(static_cast<int>(snap.spread));

            double denom = snap.bidDepth15 + snap.askDepth15 + 1e-9;
            double realImb = (snap.bidDepth15 - snap.askDepth15) / denom;
            ctx.strat->setExternalImbalance(realImb);

            // Strategy generates orders based on imbalance (LP style)
            ctx.strat->onTick(static_cast<std::uint64_t>(t), book);

            if (benchmark && (t % 10 == 0)) {
                auto bb = book.bestBid(ctx.symbol);
                auto ba = book.bestAsk(ctx.symbol);

                double bbv = bb ? static_cast<double>(bb->raw()) : -1.0;
                double bav = ba ? static_cast<double>(ba->raw()) : -1.0;

                bookStats << t << ","
                          << ctx.symbol << ","
                          << snap.mid << ","
                          << snap.spread << ","
                          << snap.bidDepth15 << ","
                          << snap.askDepth15 << ","
                          << bbv << ","
                          << bav << "\n";
            }

            // Mark-to-market PnL for this symbol
            if (benchmark && (t % 10 == 0)) {
                auto snapPnL = pnl.snapshot(ctx.symbol);
                double unreal = (snapPnL.position != 0.0
                                 ? (snap.mid - snapPnL.avgPrice) * snapPnL.position
                                 : 0.0);
                double total  = snapPnL.realizedPnL + unreal;

                pnlStats << t << ","
                         << ctx.symbol << ","
                         << snapPnL.position << ","
                         << snapPnL.avgPrice << ","
                         << snapPnL.realizedPnL << ","
                         << unreal << ","
                         << total << ","
                         << snap.mid << "\n";
            }
        }

        if (benchmark && (t % 10 == 0)) {
            perfOut << t << "," << newOrders << "\n";
        }
    }

    auto end  = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(end - start).count();
    double eps  = (secs > 0.0 ? static_cast<double>(newOrders) / secs : 0.0);

    if (benchmark) {
        std::cout << "Benchmark mode (multi-symbol)\n";
        std::cout << "  Orders generated (all symbols): " << newOrders << "\n";
        std::cout << "  Elapsed seconds:                 " << secs << "\n";
        std::cout << "  Orders/sec:                      " << eps << "\n";
        if (newOrders > 0) {
            double latency_ns = (secs * 1e9) / static_cast<double>(newOrders);
            std::cout << "  Latency per event (ns):          " << latency_ns << "\n";
        }
    }

    std::cout << "Live sim finished (multi-symbol).\n";
    std::cout << "Global checksum: " << book.checksum() << "\n";


    return 0;
}
