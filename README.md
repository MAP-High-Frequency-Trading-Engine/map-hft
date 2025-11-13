#  Map HFT Engine

A **type-safe high-frequency trading engine** built in modern C++ by the MAP team (**Mingxuan**, **Avi**, **Paul**).  
Implements a deterministic, replayable event-driven architecture for market simulation and visualization.

---

##  Week 1 – Core Runtime & Engine Foundations

###  Features Implemented
- **EventBus** – type-erased pub/sub messaging system (`include/map/core/EventBus.hpp`)  
- **Event Types** – `NewOrderEvent`, `CancelOrderEvent`, `TradeEvent` (`include/map/core/Event.hpp`)  
- **Limit Order Book** – price-time-priority add, cancel, and match operations (`include/map/book/OrderBook.hpp`)  
- **Strong Typedefs** – compile-time safety for `Price`, `Quantity`, `Notional`, `OrderId` (`include/map/types/Types.hpp`)  
- **Binary Logger** – serializes all events into `events.bin` for deterministic replay (`src/core/Logger.cpp`)  
- **Unit Tests** – Catch2 tests for OrderBook and EventBus (`tests/test_orderbook.cpp`, `tests/test_eventbus.cpp`)  
- **Smoke Test** – quick sanity check for matching logic (`tests/OrderBookSmoke.cpp`)  
- **SFML Visualizer** – basic graphical order-book viewer (`src/apps/ob_viewer.cpp`)

---
## Week 2 – Deterministic Replay, Risk Limits, and Replay Viewer

### New Components

| Component | Description |
|----------|-------------|
| **RiskLimits (`risk/RiskLimits.hpp`, `risk/RiskLimits.cpp`)** | Adds max-order-size, max-position, and max-notional checks. The OrderBook now enforces risk before accepting an order. |
| **Live Simulation (`src/apps/live_sim.cpp`)** | Runs a deterministic simulation using `BasicStrategy`. Publishes events into the EventBus and logs everything to `events.bin`. Prints a checksum for replay verification. |
| **LogReader (`replay/LogReader.cpp`)** | Sequentially decodes `events.bin` into strongly typed events for replay. |
| **OrderBook Replay Integration** | During replay, events from `LogReader` are reapplied to the OrderBook, reconstructing the exact market state from the log. |
| **SFML Viewer v2 (`ob_viewer.cpp`)** | Reads `events.bin` in replay mode, streams events through the OrderBook, and renders bids (green), asks (red), mid-price, spread, and event counter. |
| **Deterministic Clock & Replay Loop** | Viewer uses `sf::Clock` to pace event playback at a stable interval (e.g., 20 ms per event). |

---

## How to Use

### 1. Generate a deterministic event log

    cmake --build build --target live_sim
    ./build/live_sim

Produces:
- events.bin  
- a simulation checksum (e.g., `Checksum: 789`)

### 2. Verify deterministic replay

    ./build/replay_test

Example:

    Replay checksum: 789

Matching checksums confirm deterministic record → replay.

### 3. Visualize the replay

    cmake --build build --target ob_viewer
    ./build/ob_viewer

Viewer functionality:
- loads events.bin  
- replays it through the OrderBook  
- displays bids (green), asks (red), mid, spread, and total events seen  

Controls:
- Esc → exit viewer

---

## Repository Structure (Updated)

    Map/
    ├─ include/map/
    │  ├─ core/        (Event, EventBus, Logger)
    │  ├─ book/        (OrderBook, Trade)
    │  ├─ types/       (Price, Quantity, Notional)
    │  ├─ replay/      (LogReader)
    │  └─ risk/        (RiskLimits)
    │
    ├─ src/
    │  ├─ core/        (Logger.cpp)
    │  ├─ book/        (OrderBook.cpp)
    │  ├─ replay/      (LogReader.cpp)
    │  ├─ risk/        (RiskLimits.cpp)
    │  └─ apps/
    │       ├─ live_sim.cpp
    │       ├─ replay_test.cpp
    │       └─ ob_viewer.cpp
    │
    ├─ tests/          (unit + smoke + replay tests)
    ├─ assets/         (font for viewer)
    ├─ CMakeLists.txt
    └─ README.md

---

## Summary

Week 2 delivers:
- deterministic logging (events.bin)
- deterministic replay (matching checksums)
- risk-aware OrderBook enforcement
- binary LogReader
- SFML viewer driven entirely by replay data
