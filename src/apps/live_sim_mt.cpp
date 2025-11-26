// src/apps/live_sim_mt.cpp

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/core/Logger.hpp"
#include "map/core/SPSCQueue.hpp"

#include "map/OrderBook.hpp"
#include "map/Side.hpp"
#include "map/Types.hpp"

#include "map/market/RealLOBFeed.hpp"
#include "map/strategy/BasicStrategy.hpp"
#include "map/risk/PnLTracker.hpp"
#include "map/risk/ImpactModel.hpp"

#include "map/util/Rdtsc.hpp"
#include "map/util/NetJitter.hpp"

using namespace map;

// -----------------------------------------------------------------------------
// Small helper structs for this app
// -----------------------------------------------------------------------------

// Whatever RealLOBFeed exposes as its snapshot type
using Snapshot = RealSnapshot;

// Per-symbol state used by the MD thread
struct SymbolContext {
    std::string  symbol;
    RealLOBFeed  feed;
};

// Minimal market point we need downstream (for impact / PnL / strat)
struct MarketPoint {
    double mid      = 0.0;
    double bidDepth = 0.0;
    double askDepth = 0.0;
};

// -----------------------------------------------------------------------------
// Data passed between stages
// -----------------------------------------------------------------------------

struct MDUpdate {
    std::string  symbol;
    Snapshot     snap;
    std::uint64_t tick;
};

struct StratOrder {
    NewOrderEvent newOrder;
};

// Capacity chosen to be comfortably larger than expected outstanding work.
static constexpr std::size_t MD_TO_STRAT_CAP   = 1u << 16;
static constexpr std::size_t STRAT_TO_RISK_CAP = 1u << 16;

using MDQueue    = SPSCQueue<MDUpdate,   MD_TO_STRAT_CAP>;
using OrderQueue = SPSCQueue<StratOrder, STRAT_TO_RISK_CAP>;

// -----------------------------------------------------------------------------
// Market data thread (cold config + hot loop)
// -----------------------------------------------------------------------------

struct MDThreadConfig {
    std::vector<std::string> symbols;
    std::string              freq;        // e.g. "1sec"
    long long                maxTicks;    // <=0 = full file
    bool                     shockVol;
    bool                     thinBook;
    NetJitterConfig          netCfg;
    unsigned int             rngSeed;     // NEW: deterministic jitter
};

struct MDThreadContext {
    MDThreadConfig     cfg;
    MDQueue*           outQ;
    std::atomic<bool>* doneFlag;
    LatencyRecorder*   latency;    // rdtsc cycles for MD stage
};

static void md_thread_fn(MDThreadContext ctx) {
    // --- cold path: load feeds and compute min length across symbols ---
    std::vector<SymbolContext> ctxs;
    ctxs.reserve(ctx.cfg.symbols.size());

    std::size_t minSize = std::numeric_limits<std::size_t>::max();

    for (const auto& sym : ctx.cfg.symbols) {
        SymbolContext sc;
        sc.symbol = sym;
        std::string path = "data/" + sym + "_" + ctx.cfg.freq + ".csv";
        std::cout << "[md] Using " << path << " for " << sym << "\n";

        if (!sc.feed.loadCSV(path, ctx.cfg.shockVol)) {
            std::cerr << "[md] ERROR: failed to load " << path << " for " << sym << "\n";
            ctx.doneFlag->store(true);
            return;
        }
        minSize = std::min(minSize, sc.feed.size());
        ctxs.push_back(std::move(sc));
    }

    if (minSize == 0) {
        std::cerr << "[md] ERROR: no snapshots loaded.\n";
        ctx.doneFlag->store(true);
        return;
    }

    std::int64_t ticks = static_cast<std::int64_t>(minSize);
    if (ctx.cfg.maxTicks > 0 && ctx.cfg.maxTicks < ticks) {
        ticks = ctx.cfg.maxTicks;
    }

    // NEW: deterministic RNG for jitter
    std::mt19937 rng{ctx.cfg.rngSeed};

    // --- hot loop: generate MDUpdate per symbol per tick ---
    for (std::int64_t t = 0; t < ticks; ++t) {
        for (auto& sc : ctxs) {
            // Simulate network latency + jitter
            simulate_network_delay(ctx.cfg.netCfg, rng);

            std::uint64_t tsStart = rdtsc();

            const Snapshot& snap = sc.feed.at(static_cast<std::size_t>(t));

            MDUpdate upd;
            upd.symbol = sc.symbol;
            upd.snap   = snap;
            upd.tick   = static_cast<std::uint64_t>(t);

            // Lock-free push with simple spin if full
            while (!ctx.outQ->push(upd)) {
                // Busy-wait (simplicity > power efficiency for this assignment)
                std::this_thread::yield();
            }

            std::uint64_t tsEnd = rdtsc();
            ctx.latency->record(tsStart, tsEnd);
        }
    }

    ctx.doneFlag->store(true);
    std::cout << "[md] done (ticks=" << ticks << ")\n";
}

// -----------------------------------------------------------------------------
// Strategy thread
// -----------------------------------------------------------------------------

struct StratThreadConfig {
    std::vector<std::string>   symbols;
    BasicStrategy::Params      baseParams;
    bool                       useBookImbalance;
};

struct StratThreadContext {
    StratThreadConfig          cfg;
    MDQueue*                   inQ;
    OrderQueue*                outQ;
    std::atomic<bool>*         mdDoneFlag;
    std::atomic<bool>*         stratDoneFlag;
    LatencyRecorder*           latency;
};

static void strat_thread_fn(StratThreadContext ctx) {
    // --- cold: per-symbol strategy objects and last market state ---
    std::unordered_map<std::string, BasicStrategy>  strategies;
    std::unordered_map<std::string, MarketPoint>    lastMkt;
    std::unordered_map<std::string, std::uint64_t>  ticksSeen;

    strategies.reserve(ctx.cfg.symbols.size());
    lastMkt.reserve(ctx.cfg.symbols.size());
    ticksSeen.reserve(ctx.cfg.symbols.size());

    EventBus dummyBus; // not used directly; strategy publishes nowhere real

    for (const auto& sym : ctx.cfg.symbols) {
        BasicStrategy::Params p = ctx.cfg.baseParams;
        p.basePrice             = Price(std::int32_t(100)); // initial dummy mid
        p.lagInterval           = ctx.cfg.baseParams.lagInterval;

        strategies.emplace(
            sym,
            BasicStrategy(dummyBus, sym, p)
        );
        ticksSeen[sym] = 0;
    }

    auto publishOrder = [&](const std::string& sym,
                            Side               side,
                            Price              px,
                            Quantity           qty)
    {
        StratOrder so{};
        so.newOrder.symbol = sym;
        so.newOrder.side   = side;
        so.newOrder.price  = px;
        so.newOrder.qty    = qty;

        while (!ctx.outQ->push(so)) {
            std::this_thread::yield();
        }
    };

    // Hot loop: consume MDUpdate, drive strategies, emit orders
    while (true) {
        MDUpdate upd;
        bool got = ctx.inQ->pop(upd);

        if (!got) {
            if (ctx.mdDoneFlag->load() &&
                !ctx.inQ->pop(upd)) {
                break;
            }
            std::this_thread::yield();
            continue;
        }

        std::uint64_t tsStart = rdtsc();

        auto it = strategies.find(upd.symbol);
        if (it == strategies.end()) {
            continue;
        }
        BasicStrategy& strat = it->second;

        // Keep last market state for potential inventory / quoting
        MarketPoint mp;
        mp.mid      = upd.snap.mid;
        mp.bidDepth = upd.snap.bidDepth15;
        mp.askDepth = upd.snap.askDepth15;
        lastMkt[upd.symbol] = mp;

        double denom   = mp.bidDepth + mp.askDepth + 1e-9;
        double realImb = (mp.bidDepth - mp.askDepth) / denom;

        strat.setMidPrice(Price(static_cast<std::int32_t>(
            std::llround(upd.snap.mid))));
        strat.setSpread(static_cast<int>(upd.snap.spread));
        strat.setExternalImbalance(realImb);
        strat.setUseBookImbalance(ctx.cfg.useBookImbalance);

        // Drive strategy tick (book not used when useBookImbalance=false)
                static thread_local OrderBook dummyBook;
        strat.onTick(upd.tick, dummyBook); // book not used when useBookImbalance=false

        // Simple “behavioral” firing: only fire every k-th tick per symbol
               std::uint64_t& seen = ticksSeen[upd.symbol];
        ++seen;

        int ticksPerOrder = std::max(1, static_cast<int>(ctx.cfg.baseParams.ticksPerOrder));
        const std::uint64_t k = static_cast<std::uint64_t>(ticksPerOrder);

        if (seen % k == 0) {
            // Branch-minimized-ISH side selection based on imbalance
            double imb    = realImb;
            double imbAbs = std::abs(imb);

            int midTicks = static_cast<int>(std::llround(upd.snap.mid));
            if (midTicks <= 0) midTicks = 1;

            Side side;
            int  px = midTicks;
            const double lpThreshold   = ctx.cfg.baseParams.lpThreshold;
            const int    lpBaseOffset  = ctx.cfg.baseParams.lpBaseOffset;

            if (imb > lpThreshold) {
                side = Side::Ask;
                px  += lpBaseOffset + static_cast<int>(std::floor(imbAbs * 5.0));
            } else if (imb < -lpThreshold) {
                side = Side::Bid;
                px  -= lpBaseOffset + static_cast<int>(std::floor(imbAbs * 5.0));
            } else {
                side = (upd.tick % 2 == 0 ? Side::Bid : Side::Ask);
                px  += (side == Side::Bid ? -1 : +1);
            }
            if (px <= 0) px = 1;

            Quantity clip = ctx.cfg.baseParams.clipSize;

            publishOrder(upd.symbol, side, Price(px), clip);
        }

        std::uint64_t tsEnd = rdtsc();
        ctx.latency->record(tsStart, tsEnd);
    }

    ctx.stratDoneFlag->store(true);
    std::cout << "[strat] done\n";
}

// -----------------------------------------------------------------------------
// Risk + matching thread
// -----------------------------------------------------------------------------

struct RiskThreadConfig {
    std::string        logFilename;
    bool               benchmark;
};

struct RiskThreadContext {
    RiskThreadConfig   cfg;
    OrderQueue*        inQ;
    std::atomic<bool>* stratDoneFlag;
    LatencyRecorder*   latency;
};

static void risk_thread_fn(RiskThreadContext ctx) {
    // --- cold: engine wiring: EventBus + OrderBook + Risk + PnL + Impact + Logger ---
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

    Logger logger(ctx.cfg.logFilename);

    std::unordered_map<std::string, MarketPoint> lastMkt;
    std::unordered_map<std::string, double>      lastImb;

    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        logger.log(e);

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

        impact.onTrade(e.symbol, e.side, qtyD, depth, imb);

        double execPxD    = impact.executionPrice(e.symbol, e.side, mid, depth);
        int    execTicks  = std::max(1, static_cast<int>(std::llround(execPxD)));
        Price  execPx(execTicks);

        // Insert at original limit price
        book.addOrder(e.side, e.price, e.qty, e.symbol);

        // PnL at impacted execution price
        pnl.onFill(e.symbol, e.side, execPx, e.qty);
    });

    bus.subscribe<CancelOrderEvent>([&](const CancelOrderEvent& e) {
        logger.log(e);
        book.cancelOrder(e.id);
    });

    bus.subscribe<TradeEvent>([&](const TradeEvent& e) {
        logger.log(e);
        // book state implied by matching logic; not applied directly here
    });

    std::uint64_t totalOrders = 0;

    auto startWall = std::chrono::steady_clock::now();

    while (true) {
        StratOrder so;
        bool got = ctx.inQ->pop(so);

        if (!got) {
            if (ctx.stratDoneFlag->load() &&
                !ctx.inQ->pop(so)) {
                break;
            }
            std::this_thread::yield();
            continue;
        }

        std::uint64_t tsStart = rdtsc();

        bus.publish(so.newOrder);
        ++totalOrders;

        std::uint64_t tsEnd = rdtsc();
        ctx.latency->record(tsStart, tsEnd);
    }

    auto endWall  = std::chrono::steady_clock::now();
    double secs   = std::chrono::duration<double>(endWall - startWall).count();
    double eps    = (secs > 0.0 ? static_cast<double>(totalOrders) / secs : 0.0);

    std::cout << "[risk] done. Orders=" << totalOrders
              << " eps=" << eps
              << " checksum=" << book.checksum() << "\n";
}

// -----------------------------------------------------------------------------
// main: parse flags, wire pipeline, run
// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    // --- cold: config defaults ---
    std::vector<std::string> symbols = {"BTC", "ETH", "ADA"};
    std::string freq           = "1sec";
    long long   maxTicks       = -1;
    bool        shockVol       = false;
    bool        thinBook       = false;
    bool        benchmark      = true;       // always collect latency
    std::string logFilename    = "events_mt.bin";

    // simple net jitter modes
    NetJitterConfig netCfg;
    netCfg.baseMicros   = 50;
    netCfg.jitterMicros = 20;
    netCfg.enabled      = true;

    // NEW: deterministic jitter seed
    unsigned int netSeed = 12345;

    // CLI parsing (cold)
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        if (a == "--shock-vol") {
            shockVol = true;
        } else if (a == "--thin-book") {
            thinBook = true;
        } else if (a.rfind("--freq=", 0) == 0) {
            freq = a.substr(7);
        } else if (a.rfind("--max-ticks=", 0) == 0) {
            maxTicks = std::stoll(a.substr(12));
        } else if (a == "--no-net") {
            netCfg.enabled = false;
        } else if (a == "--net-low") {
            netCfg.baseMicros   = 20;
            netCfg.jitterMicros = 5;
        } else if (a == "--net-high") {
            netCfg.baseMicros   = 100;
            netCfg.jitterMicros = 50;
        } else if (a.rfind("--net-seed=", 0) == 0) {
            netSeed = static_cast<unsigned int>(std::stoul(a.substr(11)));
        }
    }

    // Strategy base params (cold)
    BasicStrategy::Params stratParams;
    stratParams.basePrice            = Price(std::int32_t(100));
    stratParams.clipSize             = Quantity(std::int32_t(5));
    stratParams.ticksPerOrder        = 3;
    stratParams.aggressivenessMode   = 1;
    stratParams.minClip              = Quantity(std::int32_t(1));
    stratParams.maxClip              = Quantity(std::int32_t(20));
    stratParams.minTicksPerOrder     = 1;
    stratParams.maxTicksPerOrder     = 10;
    stratParams.intentRecalcInterval = 2;
    stratParams.lpThreshold          = 0.20;
    stratParams.ltThreshold          = 0.60;
    stratParams.lpBaseOffset         = 2;
    stratParams.lagInterval          = 1;
    stratParams.maxInventory         = 200;

    // Queues (shared between specific producer/consumer pairs)
    MDQueue    mdToStrat;
    OrderQueue stratToRisk;

    std::atomic<bool> mdDone{false};
    std::atomic<bool> stratDone{false};

    LatencyRecorder mdLatency("md");
    LatencyRecorder stratLatency("strat");
    LatencyRecorder riskLatency("risk");

    // --- spawn threads ---

    MDThreadContext mdCtx{
        MDThreadConfig{
            symbols,
            freq,
            maxTicks,
            shockVol,
            thinBook,
            netCfg,
            netSeed
        },
        &mdToStrat,
        &mdDone,
        &mdLatency
    };

    StratThreadContext stratCtx{
        StratThreadConfig{
            symbols,
            stratParams,
            /* useBookImbalance = */ false
        },
        &mdToStrat,
        &stratToRisk,
        &mdDone,
        &stratDone,
        &stratLatency
    };

    RiskThreadContext riskCtx{
        RiskThreadConfig{
            logFilename,
            benchmark
        },
        &stratToRisk,
        &stratDone,
        &riskLatency
    };

    std::thread mdThread(md_thread_fn, mdCtx);
    std::thread stratThread(strat_thread_fn, stratCtx);
    std::thread riskThread(risk_thread_fn, riskCtx);

    mdThread.join();
    stratThread.join();
    riskThread.join();

    // --- dump latency histograms (cold) ---
    mdLatency.dumpCsv("latency_md_cycles.csv");
    stratLatency.dumpCsv("latency_strat_cycles.csv");
    riskLatency.dumpCsv("latency_risk_cycles.csv");

    std::cout << "Multi-thread pipeline finished.\n";
    std::cout << "Latency CSVs: latency_md_cycles.csv, "
              << "latency_strat_cycles.csv, "
              << "latency_risk_cycles.csv\n";

    return 0;
}
