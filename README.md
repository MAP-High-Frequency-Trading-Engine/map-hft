# Map HFT Engine

A **type-safe high-frequency trading engine** built in modern C++ by the MAP team (Mingxuan, Avi, Paul).  
This repository implements the foundational runtime, order book, and event-driven architecture for a deterministic and replayable trading simulation.

---

##  Week 1 – Core Runtime & Engine Foundations

###  Features Implemented
- **EventBus** – type-erased pub/sub messaging system (`include/map/core/EventBus.hpp`)
- **Event Types** – `NewOrderEvent`, `CancelOrderEvent`, `TradeEvent` (`include/map/core/Event.hpp`)
- **Limit Order Book** – add, cancel, and price-time-priority match operations (`include/map/OrderBook.hpp`)
- **Strong Typedefs** – compile-time safety via `Price`, `Quantity`, `Notional`, `OrderId` (`include/map/Types.hpp`)
- **Binary Logger** – serializes all events into `events.bin` for deterministic replay (`src/core/Logger.cpp`)
- **Unit Tests** – Catch2 tests for OrderBook and EventBus (`tests/test_orderbook.cpp`, `tests/test_eventbus.cpp`)
- **Smoke Test** – sanity check for matching logic (`tests/OrderBookSmoke.cpp`)
- **SFML Visualizer** – simple graphical order book viewer (`ob_viewer.cpp`)

---

##  Repository Structure

```text
map-hft/
├─ include/map/
│   ├─ core/          
│   │   ├─ Event.hpp
│   │   ├─ EventBus.hpp
│   │   └─ Logger.hpp
│   │
│   ├─ book/        
│   │   ├─ OrderBook.hpp
│   │   └─ Trade.hpp
│   │
│   ├─ types/         
│   │   ├─ Price.hpp
│   │   ├─ Qty.hpp
│   │   └─ Notional.hpp
│   │
│   └─ replay/        
│       └─ LogReader.hpp
│
├─ src/
│   ├─ core/
│   │   ├─ Logger.cpp
│   │   └─ Clock.cpp
│   ├─ book/
│   │   └─ OrderBook.cpp
│   ├─ replay/
│   │   └─ LogReader.cpp
│   └─ apps/
│       ├─ live_sim.cpp
│       ├─ ob_viewer.cpp
│       └─ strategy_demo.cpp
│
├─ tests/
│   ├─ test_orderbook.cpp
│   ├─ test_eventbus.cpp
│   └─ OrderBookSmoke.cpp
│
├─ CMakeLists.txt
├─ .github/workflows/ci.yml
└─ README.md

```
---

# Build Instructions
## Prerequisites

- C++20 or newer compiler (AppleClang, g++, or MSVC)

- CMake 3.23+

- SFML 3.x (for the ob_viewer visualizer)

 <ins>Build</ins>
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

 <ins> Run the Smoke Test</ins>
 ```bash
./build/orderbook_smoke
```

 <ins> Run All Unit Tests</ins>
  ```bash
ctest --test-dir build --output-on-failure
```

 <ins> Run the Live Demo </ins>
  ```bash
./build/map_hft_main
```
 
 SFML Order Book Viewer

The SFML-based viewer (ob_viewer.cpp) visualizes the order book:

- Green → bids

- Red → asks

- Displays mid price and spread

- Shows a moving sweep line

- Press Spacebar to inject random orders

Run after building:
  ```bash
./build/ob_viewer
```

### Continuous Integration (CI)

GitHub Actions workflow:   ```bash.github/workflows/ci.yml```

This workflow runs automatically on every push or pull request:

1. Installs dependencies (CMake, SFML)

2. Configures project using Ninja

3. Builds all targets

4. Runs all tests with ctest

The CI badge will appear here once the workflow passes 
