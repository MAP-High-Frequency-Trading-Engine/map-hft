##  Project Overview (PDF)
[![Project PDF](https://img.shields.io/badge/View-PDF-blue)](MAP-3.pdf)


# MAP – A Deterministic High-Frequency Trading Engine

This repository contains **MAP**, a toy high-frequency trading (HFT) engine written in modern C++.  
It simulates multi-asset limit order books, runs a simple liquidity-providing strategy against **real market data**, and produces detailed CSV logs for **latency**, **order book microstructure**, and **PnL** analysis.

The code is structured like a small production engine:

- Strongly-typed prices, quantities, and order IDs
- An event bus + binary logger / replay path
- A price–time priority limit order book
- Risk limits and inventory management
- A simple liquidity-providing strategy with an impact model
- Single-threaded and multi-threaded simulation apps
- CSV outputs designed for downstream analysis (e.g., in Colab)

---

## Table of Contents

1. [Project Layout](#project-layout)  
2. [Core Concepts & Components](#core-concepts--components)  
3. [Apps / Binaries](#apps--binaries)  
4. [Data Requirements](#data-requirements)  
5. [Building](#building)  
6. [Running Simulations](#running-simulations)  
   - [Single-threaded live sim (`live_sim`)【PnL / microstructure】](#singlethreaded-live-sim_live_simpnl--microstructure)  
   - [Multi-threaded pipeline (`live_sim_mt`)【latency】](#multithreaded-pipeline_live_sim_mtlatency)  
   - [Replay test (`replay_test`)](#replay-test_replay_test)  
7. [CSV Outputs & Analysis](#csv-outputs--analysis)  
8. [Testing](#testing)  
9. [License](#license)

---

## Project Layout

High-level structure (names may vary slightly depending on your clone):

```text
.
├── CMakeLists.txt
├── include/
│   └── map/
│       ├── core/
│       │   ├── Event.hpp
│       │   ├── EventBus.hpp
│       │   ├── Logger.hpp
│       │   └── SPSCQueue.hpp
│       ├── market/
│       │   ├── MarketDataFeed.hpp      (CSVLOBFeed)
│       │   └── RealLOBFeed.hpp
│       ├── replay/
│       │   └── LogReader.hpp
│       ├── risk/
│       │   ├── ImpactModel.hpp
│       │   ├── PnLTracker.hpp
│       │   └── RiskLimits.hpp
│       ├── strategy/
│       │   └── BasicStrategy.hpp
│       ├── util/
│       │   ├── Rdtsc.hpp
│       │   └── NetJitter.hpp
│       ├── OrderBook.hpp
│       ├── Order.hpp
│       ├── Side.hpp
│       ├── StrongHash.hpp
│       └── Types.hpp
├── src/
│   ├── apps/
│   │   ├── live_sim.cpp
│   │   ├── live_sim_mt.cpp
│   │   ├── replay_test.cpp
│   │   └── main.cpp
│   ├── core/
│   │   └── Logger.cpp
│   ├── market/
│   │   └── MarketDataFeed.cpp
│   ├── replay/
│   │   └── LogReader.cpp
│   ├── risk/
│   │   ├── ImpactModel.cpp
│   │   ├── PnLTracker.cpp
│   │   └── RiskLimits.cpp
│   └── OrderBook.cpp
├── tests/
│   ├── test_eventbus.cpp
│   ├── test_orderbook.cpp
│   ├── test_replay.cpp
│   ├── test_risk.cpp
│   └── test_stability.cpp
└── data/
    ├── BTC_1sec.csv
    ├── ETH_1sec.csv
    └── ADA_1sec.csv
````

---

## Core Concepts & Components

### Strong Types (`Types.hpp` / `StrongHash.hpp`)

All key numeric domains use **strong typedefs** to avoid mixing units:

* `OrderId = Strong<OrderIdTag, std::uint64_t>`
* `Price   = Strong<PriceTag,   double>`
* `Quantity= Strong<QuantityTag, std::int32_t>`
* `Notional= Strong<NotionalTag, std::uint64_t>`

`StrongHash.hpp` provides `std::hash` specializations so strong types can be used as keys in `std::unordered_map`.

---

### Events & Event Bus (`Event.hpp`, `EventBus.hpp`, `Logger`, `LogReader`)

Core event types:

* `NewOrderEvent { symbol, side, price, qty }`
* `CancelOrderEvent { symbol, id }`
* `TradeEvent { symbol, takerId, makerId, takerSide, price, qty }`

`EventBus` is a simple **type-erased pub/sub**:

```cpp
EventBus bus;

bus.subscribe<NewOrderEvent>([](const NewOrderEvent& e) {
    // handle new order
});

NewOrderEvent e{/*...*/};
bus.publish(e);
```

**Binary logging + replay:**

* `Logger` writes events to a binary log (`events.bin`) using a compact custom format.
* `LogReader` can read these logs back and return a `std::variant<NewOrderEvent, CancelOrderEvent, TradeEvent>`.

The `replay_test` app and `tests/test_replay.cpp` prove that **logging + replay produce the same order book checksum**.

---

### Limit Order Book (`Order.hpp`, `OrderBook.hpp`)

A price–time priority book supporting:

* Multiple symbols (book is keyed by symbol)
* Separate maps for `bids` and `asks` (price → FIFO queue of `Order`)
* Matching on aggressive add:

  * Bid crosses best ask ⇒ trade
  * Ask crosses best bid ⇒ trade
* Resting orders keyed in an `IndexMap` (`OrderId → {symbol, side, price}`) for **O(1)** lookup on cancel.

Key methods:

* `OrderId addOrder(Side side, Price px, Quantity qty, const std::string& symbol)`
* `bool cancelOrder(OrderId id)`
* `std::optional<Price> bestBid(const std::string& symbol)`
* `std::optional<Price> bestAsk(const std::string& symbol)`
* `Quantity totalDepth(symbol, side, maxLevels)`
* `double orderImbalance(symbol, maxLevels)`
* `std::uint64_t checksum() const` – FNV-1a style hash of entire book state

There are also **branch-aware** helpers like:

* `bestPriceBranchless(Side side, const std::string& symbol)`
* `midBranchless(const std::string& symbol)`

used in the sims to avoid `if (side == Bid)` hot-path branches.

---

### Risk Limits & PnL (`risk/RiskLimits.*`, `risk/PnLTracker.*`)

There are two layers of “risk” in the project:

1. **Per-order & per-symbol limits** (used by `OrderBook`):

   * `RiskLimits::checkOrder(symbol, side, qty)`
   * Max order size (`maxOrderQty`)
   * Max net position (`maxPositionQty`)
   * Tracks `PositionState { netQty, totalNotional }`

2. **Global kill-switch logic in `live_sim`**:

   * Global PnL drawdown threshold
   * Realized mid-volatility threshold (std dev over rolling window)
   * Spread “explosion” relative to a learned baseline

`PnLTracker`:

* Tracks **signed position**, **average entry price**, and **realized PnL** per symbol.
* Handles both “adding to” and “reducing/flipping” positions.
* `snapshot(symbol)` returns a `PnLSnapshot`.
* `markToMarket(symbol, mid)` is available for external unrealized PnL calculations.

---

### Impact Model (`risk/ImpactModel.*`)

The engine simulates simple **price impact & adverse selection**:

* `ImpactParams` controls:

  * Temporary impact half-life
  * Temporary and permanent impact coefficients
  * Max impact in ticks
  * EWMA volatility parameter
* `ImpactModel::onNewTick`:

  * Tracks EWMA of mid changes as volatility proxy
  * Decays temporary impact toward zero
* `ImpactModel::onTrade`:

  * Updates temporary + permanent impact based on:

    * Trade size vs depth
    * LOB imbalance
    * Volatility
* `effectiveMid` & `executionPrice`:

  * Compute a strategy-perceived mid and an execution price that includes impact and spread.

The **live simulators** mark PnL to this impacted execution price, not to raw mid.

---

### Strategy (`strategy/BasicStrategy.*`)

`BasicStrategy` is a simple liquidity-providing strategy:

* Uses **order book imbalance** and/or **external imbalance** from real LOB data:

  * Imbalance > threshold ⇒ quote on ASK side, slightly above mid
  * Imbalance < −threshold ⇒ quote on BID side, slightly below mid
  * Near zero ⇒ small jitter around mid, both sides
* Adapts:

  * `currentTicksPerOrder_` (firing rate)
  * `currentClip_` (order size)
* Has a notion of **inventory** (`maxInventory`) to avoid unbounded accumulation
* Publishes `NewOrderEvent` via `EventBus`.

Parameters are grouped in `BasicStrategy::Params` and are passed in via `RunConfig` / `StratThreadConfig`.

---

### Market Data Feeds (`market/RealLOBFeed.*`, `market/MarketDataFeed.*`)

Two ways to read real LOB data:

1. **`RealLOBFeed`**

   * Lightweight struct `RealSnapshot { mid, spread, bidDepth15, askDepth15 }`
   * Reads CSV with:

     * `midpoint`, `spread`
     * `bids_market_notional_1..15`
     * `asks_market_notional_1..15`
   * Optionally takes a `shockVol` flag that **jitters mid & spread** deterministically for stress testing.
   * Used by both `live_sim` and `live_sim_mt`.

2. **`CSVLOBFeed`**

   * More detailed snapshot (`LOBSnapshot`) with distances & limit notionals at each level.
   * Column layout based on the Kaggle dataset:

     * `bids_distance_0..14`, `bids_limit_notional_0..14`
     * `asks_distance_0..14`, `asks_limit_notional_0..14`
   * Not directly used in the main apps, but useful for richer experiments.

---

### Lock-Free Queue & Latency (`SPSCQueue`, `Rdtsc`, `NetJitter`)

The multi-threaded sim (`live_sim_mt`) uses:

* `SPSCQueue<T, Capacity>` – a **single-producer / single-consumer** ring buffer:

  * `bool push(const T&)` and `bool pop(T&)`
  * Lock-free with `std::atomic<size_t>` head/tail indices
* `LatencyRecorder` – wraps `rdtsc()`:

  * `record(start, end)` stores cycle deltas
  * `dumpCsv("latency_md_cycles.csv")` writes `sample,cycles` CSV
* `NetJitter`:

  * `simulate_network_delay(NetJitterConfig, RNG&)` simulates configurable base latency + jitter in microseconds
  * Used in the **MD thread** to simulate noisy hardware / network environments.

---

## Apps / Binaries

The main executables built by CMake:

* `map_hft_main` – tiny demo of the `OrderBook`
* `live_sim` – single-threaded multi-symbol live simulator (PnL, book stats, risk kill switch)
* `live_sim_mt` – multi-threaded pipeline (MD → Strat → Risk) with latency measurement
* `replay_test` – replays binary logs and measures replay speed
* `orderbook_smoke` – simple smoke test binary
* `unit_tests` – Catch2 unit tests

---

## Data Requirements

The simulators expect real LOB CSVs in `data/` with names:

* `data/BTC_1sec.csv`
* `data/ETH_1sec.csv`
* `data/ADA_1sec.csv`

`RealLOBFeed` assumes at least these columns:

* `midpoint`
* `spread`
* `bids_market_notional_1` … `bids_market_notional_15`
* `asks_market_notional_1` … `asks_market_notional_15`

If your dataset differs, adjust the column names / indexes in `RealLOBFeed::loadCSV`.

---

## Building

Requires a reasonably modern C++ toolchain (C++20/23) and CMake.

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build everything
cmake --build build -j
```

This should produce binaries under `build/`:

```text
build/
├── map_hft_main
├── live_sim
├── live_sim_mt
├── replay_test
├── orderbook_smoke
└── unit_tests
```

---

## Running Simulations

### Single-threaded live sim (`live_sim`)【PnL / microstructure】

`live_sim` runs **one or two strategy configs** (baseline + optional aggressive) against real data and can emit CSVs for:

* Performance over time: `perf_live_{label}.csv`
* Order book stats:      `book_stats_{label}.csv`
* PnL stats:             `pnl_stats_{label}.csv`

Key flags:

* `--benchmark` – enable CSV outputs
* `--compare-strats` – run both baseline and aggressive configs
* `--freq=1sec` – choose data frequency (`BTC_1sec.csv`, etc.)
* `--max-ticks=N` – cap the number of ticks
* `--shock-vol` – amplify spread / volatility (stress)
* `--thin-book` – scale down depth to mimic illiquidity
* `--freeze-book=N` – reuse previous snapshot index every N ticks
* `--delay-feed=MS` – slow down MD loop by MS per tick
* `--no-risk` – disable global kill switch checks
* `--lag-strategy=K` – adjust how often the strategy recomputes “intent”

Example: baseline vs aggressive, benchmark on 200k ticks:

```bash
./build/live_sim \
  --benchmark \
  --compare-strats \
  --freq=1sec \
  --max-ticks=200000
```

This creates:

* `perf_live_baseline.csv`
* `perf_live_aggressive.csv`
* `book_stats_baseline.csv`
* `book_stats_aggressive.csv`
* `pnl_stats_baseline.csv`
* `pnl_stats_aggressive.csv`

Each `{label}` corresponds to a `RunConfig` (`baseline` / `aggressive`) defined in `live_sim.cpp`.

---

### Multi-threaded pipeline (`live_sim_mt`)【latency】

`live_sim_mt` uses three threads:

1. **MD thread** – reads `RealLOBFeed`, simulates network jitter, and pushes `MDUpdate` events.
2. **Strategy thread** – consumes MD updates, runs `BasicStrategy`, and pushes `StratOrder`s.
3. **Risk thread** – consumes orders, runs `EventBus + Logger + OrderBook + PnL + ImpactModel`.

It always produces **latency CSVs**:

* `latency_md_cycles.csv`
* `latency_strat_cycles.csv`
* `latency_risk_cycles.csv`

Flags:

* `--freq=1sec` – which CSVs to load (`BTC_1sec.csv`, etc.)
* `--max-ticks=N` – cap number of ticks
* Network modes:

  * `--no-net`     – disable network jitter
  * `--net-low`    – low latency / jitter (e.g., 20µs ± 5µs)
  * `--net-high`   – higher latency / jitter (e.g., 100µs ± 50µs)
  * `--net-seed=S` – seed for deterministic jitter RNG
* Stress flags:

  * `--shock-vol` – enable `RealLOBFeed` mid/spread jitter
  * `--thin-book` – scale down depth

Example: generate latency CSVs for a few regimes (rename between runs):

```bash
# Baseline / no network jitter
./build/live_sim_mt --freq=1sec --max-ticks=200000 --no-net
mv latency_md_cycles.csv    latency_md_cycles_none.csv
mv latency_strat_cycles.csv latency_strat_cycles_none.csv
mv latency_risk_cycles.csv  latency_risk_cycles_none.csv

# High jitter
./build/live_sim_mt --freq=1sec --max-ticks=200000 --net-high
mv latency_md_cycles.csv    latency_md_cycles_high.csv
mv latency_strat_cycles.csv latency_strat_cycles_high.csv
mv latency_risk_cycles.csv  latency_risk_cycles_high.csv
```

These CSVs are intended for downstream analysis (histograms, ECDFs, p99, etc.).

---

### Replay test (`replay_test`)

`replay_test` reads a binary log (default `events.bin`), replays all events into a fresh `OrderBook`, and reports replay throughput.

```bash
# Run some live sim that logs events (when logging is enabled in your build)
./build/live_sim ...

# Then replay
./build/replay_test events.bin
```

It prints:

* Total events replayed
* Elapsed seconds
* Events / second
* Final book checksum

It also writes a small CSV:

* `perf_replay.csv` with `events,seconds,events_per_sec`.

---

## CSV Outputs & Analysis

The engine is designed to work nicely with analysis notebooks (e.g., Google Colab).

Typical CSVs:

* **Latency** (from `live_sim_mt`):

  * `latency_md_cycles*.csv`
  * `latency_strat_cycles*.csv`
  * `latency_risk_cycles*.csv`
  * Columns: `sample,cycles`

* **Performance & PnL** (from `live_sim` with `--benchmark`):

  * `perf_live_{label}.csv`

    * `tick,orders`
  * `book_stats_{label}.csv`

    * `tick,symbol,real_mid,real_spread,real_bidDepth,real_askDepth,engine_bestBid,engine_bestAsk`
  * `pnl_stats_{label}.csv`

    * `tick,symbol,position,avg_price,realized,unrealized,total,mid`

These are sufficient to build:

* p50 / p99 latency plots by stage & regime
* Total orders vs ticks, per strategy
* PnL trajectories (per symbol & aggregated)
* Per-symbol microstructure summaries (average spread, average depth, tracking error vs real mid, etc.)

---

## Testing

Catch2 unit tests cover core components:

* Event routing (`EventBus`)
* Order book behavior (no cross, full match, partial fill)
* Risk limits (qty sanity, max order size, max position)
* Replay correctness (live vs replay checksum match)
* A basic high-volume stability test (10k events through `EventBus` + `OrderBook`)

Run all tests:

```bash
./build/unit_tests
```

There’s also a tiny smoke binary:

```bash
./build/orderbook_smoke
```

which runs a few small scenarios and prints:

```text
OrderBook smoke tests passed
```

---



```
```
