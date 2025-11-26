// src/apps/live_sim.cpp
#include <iostream>
#include <string>
#include <cstdint>
#include <thread>
#include <chrono>
#include <fstream>
#include <vector>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <deque>
#include <numeric>
#include <cmath>

#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/core/Logger.hpp"
#include "map/OrderBook.hpp"
#include "map/strategy/BasicStrategy.hpp"
#include "map/market/RealLOBFeed.hpp"
#include "map/risk/PnLTracker.hpp"
#include "map/risk/ImpactModel.hpp"

using namespace map;

// -----------------------------------------------------------------------------
// Small helper types
// -----------------------------------------------------------------------------

struct SymbolContext {
    std::string    symbol;
    RealLOBFeed    feed;
    BasicStrategy* strat = nullptr;  // non-owning
};

struct MarketPoint {
    double mid       = 0.0;
    double bidDepth  = 0.0;
    double askDepth  = 0.0;
};

struct RunConfig {
    std::string           label;   // "baseline", "aggressive", etc.
    BasicStrategy::Params params;
};

// Simple helper: std dev of a deque
static double compute_stddev(const std::deque<double>& xs) {
    if (xs.size() < 2) return 0.0;
    double mean = std::accumulate(xs.begin(), xs.end(), 0.0) /
                  static_cast<double>(xs.size());
    double var = 0.0;
    for (double x : xs) {
        double d = x - mean;
        var += d * d;
    }
    var /= static_cast<double>(xs.size() - 1);
    return std::sqrt(std::max(0.0, var));
}

// -----------------------------------------------------------------------------
// Core simulation for ONE strategy config
// -----------------------------------------------------------------------------

static void run_sim(const RunConfig& cfg,
                    const std::string& freq,
                    bool benchmark,
                    bool shockVol,
                    bool thinBook,
                    int  lagStrategy,
                    long long maxTicks,
                    int  freezeBook,
                    int  delayMs)
{
    std::vector<std::string> symbols = {"BTC", "ETH", "ADA"};

    std::cout << "=== Run: " << cfg.label << " ===\n";
    std::cout << "Live sim: multi-symbol (BTC, ETH, ADA) freq=" << freq << "\n";

    // Stress config logging
    if (shockVol) {
        std::cout << "  [stress] shock-vol enabled (amplify spread)\n";
    }
    if (thinBook) {
        std::cout << "  [stress] thin-book enabled (scale top-15 depth down)\n";
    }
    if (freezeBook > 0) {
        std::cout << "  [stress] freeze-book every " << freezeBook
                  << " ticks (re-use previous snapshot index)\n";
    }
    if (delayMs > 0) {
        std::cout << "  [stress] delay-feed = " << delayMs
                  << " ms per tick (slow market data)\n";
    }

    // -------------------------------------------------------------------------
    // Load real LOB feeds for each symbol
    // -------------------------------------------------------------------------
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
            return;
        }
        minSize = std::min(minSize, ctx.feed.size());

        ctxs.push_back(std::move(ctx));
    }

    if (minSize == 0) {
        std::cerr << "ERROR: no snapshots loaded.\n";
        return;
    }

    // -------------------------------------------------------------------------
    // Engine wiring: EventBus + OrderBook + PnL + ImpactModel
    // -------------------------------------------------------------------------
    EventBus   bus;
    OrderBook  book;
    PnLTracker pnl;

    ImpactParams impParams;
    impParams.tempHalfLifeTicks = 800.0;
    impParams.tempCoeff         = 2.0;
    impParams.permCoeff         = 0.5;
    impParams.maxImpactTicks    = 100.0;
    impParams.volAlpha          = 0.05;

    ImpactModel impact(impParams);

    // Last observed market point & imbalance per symbol
    std::unordered_map<std::string, MarketPoint> lastMkt;
    std::unordered_map<std::string, double>      lastImb;

#ifndef MAP_BENCHMARK_MODE
    Logger logger("events.bin");
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) { logger.log(e); });
    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) { logger.log(e); });
    bus.subscribe<TradeEvent>([&](const TradeEvent& e) { logger.log(e); });
#endif

    std::uint64_t newOrders = 0;

    // Book + PnL wiring including impact model
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        double mid   = static_cast<double>(e.price.raw());
        double depth = 1.0;
        double imb   = 0.0;

        if (auto it = lastMkt.find(e.symbol); it != lastMkt.end()) {
            mid   = it->second.mid;
            depth = std::max(1.0, it->second.bidDepth + it->second.askDepth);
        }
        if (auto it2 = lastImb.find(e.symbol); it2 != lastImb.end()) {
            imb = it2->second;
        }

        double qtyD = static_cast<double>(e.qty.raw());

        // Update adverse-selection / impact
        impact.onTrade(e.symbol, e.side, qtyD, depth, imb);

        // Execution price with impact
        double execPxD = impact.executionPrice(e.symbol, e.side, mid, depth);
        int    execPxTicks = std::max(1, static_cast<int>(std::llround(execPxD)));
        Price  execPx(execPxTicks);

        // Insert into internal book at limit price
        book.addOrder(e.side, e.price, e.qty, e.symbol);
        ++newOrders;

        // PnL marked to impacted execution price
        pnl.onFill(e.symbol, e.side, execPx, e.qty);
    });

    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) {
        book.cancelOrder(e.id);
    });

    // -------------------------------------------------------------------------
    // Strategies per symbol
    // -------------------------------------------------------------------------
    std::vector<BasicStrategy> strategies;
    strategies.reserve(ctxs.size());

    for (auto& ctx : ctxs) {
        BasicStrategy::Params p = cfg.params;

        p.basePrice   = Price(std::int32_t(100));
        p.lagInterval = lagStrategy;

        strategies.emplace_back(bus, ctx.symbol, p);
        ctx.strat = &strategies.back();

        // We rely on external imbalance from RealLOBFeed
        ctx.strat->setUseBookImbalance(false);
    }

    // -------------------------------------------------------------------------
    // CSV outputs for benchmark mode
    // -------------------------------------------------------------------------
    std::ofstream perfOut;
    std::ofstream bookStats;
    std::ofstream pnlStats;

    if (benchmark) {
        std::string suffix = "_" + cfg.label + ".csv";

        perfOut.open("perf_live" + suffix);
        perfOut << "tick,orders\n";

        bookStats.open("book_stats" + suffix);
        bookStats << "tick,symbol,real_mid,real_spread,real_bidDepth,real_askDepth,"
                  << "engine_bestBid,engine_bestAsk\n";

        pnlStats.open("pnl_stats" + suffix);
        pnlStats << "tick,symbol,position,avg_price,realized,unrealized,total,mid\n";
    }

    // -------------------------------------------------------------------------
    // Single-threaded risk / kill-switch state
    // -------------------------------------------------------------------------
    const double KILL_PNL_THRESHOLD   = -1000.0; // PnL drawdown
    const double KILL_VOL_THRESHOLD   = 50.0;    // realized mid-vol
    const double KILL_SPREAD_MULT     = 10.0;    // spread 10x baseline
    const int    VOL_WINDOW           = 200;
    const int    BASELINE_SPREAD_WIN  = 200;

    std::unordered_map<std::string, std::deque<double>> midHist;
    std::unordered_map<std::string, double>             baselineSpread;
    std::unordered_map<std::string, std::size_t>        baselineCount;
    std::unordered_map<std::string, bool>               baselineLocked;

    bool   killSwitch = false;
    double minPnL     = 0.0;

    // -------------------------------------------------------------------------
    // Main simulation loop
    // -------------------------------------------------------------------------
    std::int64_t ticks = static_cast<std::int64_t>(minSize);
    if (maxTicks > 0 && maxTicks < ticks) {
        ticks = maxTicks;
    }

    auto start = std::chrono::steady_clock::now();

    for (std::int64_t t = 0; t < ticks; ++t) {
        if (killSwitch) {
            std::cout << "[risk] Kill switch active, stopping run.\n";
            break;
        }

        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }

        double totalPnL = 0.0;

        for (auto& ctx : ctxs) {
            // freeze-book: every Nth tick, reuse previous snapshot index
            std::size_t idx = static_cast<std::size_t>(t);
            if (freezeBook > 0 && t > 0 && (t % freezeBook) == 0) {
                idx = static_cast<std::size_t>(t - 1);
            }

            const RealSnapshot& snap = ctx.feed.at(idx);

            double mid    = snap.mid;
            double spread = snap.spread;

            // Shock-vol: amplify spread
            if (shockVol) {
                spread *= 2.0;
            }

            double bidDepth = snap.bidDepth15;
            double askDepth = snap.askDepth15;
            if (thinBook) {
                bidDepth *= 0.7;
                askDepth *= 0.7;
            }

            // Impact model tick update
            impact.onNewTick(ctx.symbol, mid, spread);
            MarketPoint mp;
            mp.mid      = mid;
            mp.bidDepth = bidDepth;
            mp.askDepth = askDepth;
            lastMkt[ctx.symbol] = mp;

            double denom   = bidDepth + askDepth + 1e-9;
            double realImb = (bidDepth - askDepth) / denom;
            lastImb[ctx.symbol] = realImb;

            double effMid = impact.effectiveMid(ctx.symbol, mid);
            ctx.strat->setMidPrice(Price(static_cast<std::int32_t>(
                std::llround(effMid))));
            ctx.strat->setSpread(static_cast<int>(spread));
            ctx.strat->setExternalImbalance(realImb);

            ctx.strat->onTick(static_cast<std::uint64_t>(t), book);

            // Book stats
if (benchmark && (t % 10 == 0)) {
    auto bbOpt = book.bestPriceBranchless(Side::Bid, ctx.symbol);
    auto baOpt = book.bestPriceBranchless(Side::Ask, ctx.symbol);

    double bbv = bbOpt ? static_cast<double>(bbOpt->raw()) : -1.0;
    double bav = baOpt ? static_cast<double>(baOpt->raw()) : -1.0;

    bookStats << t << ","
              << ctx.symbol << ","
              << mid << ","
              << spread << ","
              << bidDepth << ","
              << askDepth << ","
              << bbv << ","
              << bav << "\n";
}

            // PnL stats
            auto snapPnL = pnl.snapshot(ctx.symbol);
            double unreal = (snapPnL.position != 0.0
                             ? (mid - snapPnL.avgPrice) * snapPnL.position
                             : 0.0);
            double total  = snapPnL.realizedPnL + unreal;
            totalPnL += total;

            if (benchmark && (t % 10 == 0)) {
                pnlStats << t << ","
                         << ctx.symbol << ","
                         << snapPnL.position << ","
                         << snapPnL.avgPrice << ","
                         << snapPnL.realizedPnL << ","
                         << unreal << ","
                         << total << ","
                         << mid << "\n";
            }

            // --- Single-threaded risk monitoring ---

            auto& dq = midHist[ctx.symbol];
            dq.push_back(mid);
            if (dq.size() > static_cast<std::size_t>(VOL_WINDOW)) {
                dq.pop_front();
            }

            auto& cnt      = baselineCount[ctx.symbol];
            auto& lockFlag = baselineLocked[ctx.symbol];
            if (!lockFlag) {
                baselineSpread[ctx.symbol] += spread;
                cnt += 1;
                if (cnt >= static_cast<std::size_t>(BASELINE_SPREAD_WIN)) {
                    baselineSpread[ctx.symbol] /= static_cast<double>(cnt);
                    lockFlag = true;
                }
            }
        } // per-symbol loop

        if (benchmark && (t % 10 == 0)) {
            perfOut << t << "," << newOrders << "\n";
        }

        // Global PnL kill
        minPnL = std::min(minPnL, totalPnL);
        if (totalPnL < KILL_PNL_THRESHOLD) {
            std::cout << "[risk] Kill switch (PnL) triggered: "
                      << totalPnL << "\n";
            killSwitch = true;
        }

        // Volatility kill (use BTC as proxy for whole basket)
        {
            auto it = midHist.find("BTC");
            if (it != midHist.end()) {
                double vol = compute_stddev(it->second);
                if (vol > KILL_VOL_THRESHOLD) {
                    std::cout << "[risk] Kill switch (volatility) triggered: "
                              << vol << "\n";
                    killSwitch = true;
                }
            }
        }

        // Spread explosion kill (per symbol)
        for (const auto& ctx : ctxs) {
            auto bsIt = baselineSpread.find(ctx.symbol);
            auto blIt = baselineLocked.find(ctx.symbol);
            if (bsIt != baselineSpread.end() && blIt != baselineLocked.end()
                && blIt->second)
            {
                double baseSp = bsIt->second;
                const RealSnapshot& snap = ctx.feed.at(
                    std::min<std::size_t>(t, ctx.feed.size() - 1));
                double curSpread = snap.spread * (shockVol ? 2.0 : 1.0);
                if (baseSp > 0.0 &&
                    curSpread > baseSp * KILL_SPREAD_MULT) {
                    std::cout << "[risk] Kill switch (spread explosion) for "
                              << ctx.symbol << ": current="
                              << curSpread
                              << " baseline=" << baseSp << "\n";
                    killSwitch = true;
                }
            }
        }
    } // tick loop

    auto end  = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(end - start).count();
    double eps  = (secs > 0.0 ? static_cast<double>(newOrders) / secs : 0.0);

    if (benchmark) {
        std::cout << "[run=" << cfg.label << "] Benchmark mode (multi-symbol)\n";
        std::cout << "  Orders generated (all symbols): " << newOrders << "\n";
        std::cout << "  Elapsed seconds:                 " << secs << "\n";
        std::cout << "  Orders/sec:                      " << eps << "\n";
        if (newOrders > 0) {
            double latency_ns = (secs * 1e9) / static_cast<double>(newOrders);
            std::cout << "  Latency per event (ns):          " << latency_ns << "\n";
        }
    }

    std::cout << "Run '" << cfg.label << "' finished.\n";
    std::cout << "Global checksum: " << book.checksum() << "\n";
}

// -----------------------------------------------------------------------------
// main: CLI + config + run one or two configs
// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    bool benchmark     = false;
    bool shockVol      = false;
    bool thinBook      = false;
    int  lagStrategy   = 1;
    bool compareStrats = false;

    int       freezeBook = 0;   // 0 = disabled
    int       delayMs    = 0;   // 0 = no delay
    long long maxTicks   = -1;  // <=0 = no cap

    std::string freq = "1sec";

    // Parse CLI
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        if (a == "--benchmark") {
            benchmark = true;
        } else if (a == "--shock-vol") {
            shockVol = true;
        } else if (a == "--thin-book") {
            thinBook = true;
        } else if (a.rfind("--lag-strategy=", 0) == 0) {
            lagStrategy = std::stoi(a.substr(15));
            if (lagStrategy < 1) lagStrategy = 1;
        } else if (a.rfind("--freq=", 0) == 0) {
            freq = a.substr(7);
        } else if (a == "--compare-strats") {
            compareStrats = true;
        } else if (a.rfind("--max-ticks=", 0) == 0) {
            maxTicks = std::stoll(a.substr(12));
        } else if (a.rfind("--freeze-book=", 0) == 0) {
            freezeBook = std::stoi(a.substr(14));
            if (freezeBook < 0) freezeBook = 0;
        } else if (a.rfind("--delay-feed=", 0) == 0) {
            delayMs = std::stoi(a.substr(13));
            if (delayMs < 0) delayMs = 0;
        }
    }

    // Baseline config
    RunConfig baseline;
    baseline.label = "baseline";
    baseline.params.basePrice          = Price(std::int32_t(100));
    baseline.params.clipSize           = Quantity(std::int32_t(5));
    baseline.params.ticksPerOrder      = 3;
    baseline.params.aggressivenessMode = 1;
    baseline.params.minClip            = Quantity(std::int32_t(1));
    baseline.params.maxClip            = Quantity(std::int32_t(20));
    baseline.params.minTicksPerOrder   = 1;
    baseline.params.maxTicksPerOrder   = 10;
    baseline.params.intentRecalcInterval = 2;
    baseline.params.lpThreshold        = 0.20;
    baseline.params.ltThreshold        = 0.60;
    baseline.params.lpBaseOffset       = 2;
    baseline.params.lagInterval        = 1;   // overridden by lagStrategy in run_sim
    baseline.params.maxInventory       = 200;

    // Aggressive config
    RunConfig aggressive = baseline;
    aggressive.label = "aggressive";
    aggressive.params.aggressivenessMode    = 2;
    aggressive.params.intentRecalcInterval  = 1;
    aggressive.params.minTicksPerOrder      = 1;
    aggressive.params.maxTicksPerOrder      = 5;
    aggressive.params.minClip               = Quantity(std::int32_t(5));
    aggressive.params.maxClip               = Quantity(std::int32_t(40));
    aggressive.params.maxInventory          = 400;

    if (compareStrats) {
        run_sim(baseline,   freq, benchmark, shockVol, thinBook,
                lagStrategy, maxTicks, freezeBook, delayMs);
        run_sim(aggressive, freq, benchmark, shockVol, thinBook,
                lagStrategy, maxTicks, freezeBook, delayMs);
    } else {
        run_sim(baseline,   freq, benchmark, shockVol, thinBook,
                lagStrategy, maxTicks, freezeBook, delayMs);
    }

    return 0;
}
