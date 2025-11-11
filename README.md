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

##  Week 2 – Enhancements & Replay Viewer

###  New Components

| Component | Description |
|------------|-------------|
| **Live Simulation (`live_sim.cpp`)** | Generates synthetic add/cancel/match events and writes them deterministically to `events.bin`. |
| **LogReader (`replay/LogReader.cpp`)** | Streams serialized events from `events.bin` into the engine, reconstructing exact market state for playback. |
| **OrderBook Replay Integration** | OrderBook updates in real time as events are replayed, enabling accurate market reconstruction. |
| **SFML Viewer v2 (`ob_viewer.cpp`)** | Reads `events.bin`, renders bids (green) and asks (red) over time, shows mid-price and spread, includes pause/resume controls. |
| **Deterministic Clock & Replay Loop** | Uses `sf::Clock` to advance events every 20 ms for consistent playback speed. |

---

###  How to Use

#### 1. Generate a New Event Log
```bash
cmake --build build --target live_sim
./build/live_sim        # creates or overwrites events.bin
```
#### 2. Replay and Visualize
```bash
cmake --build build --target ob_viewer
./build/ob_viewer       # opens SFML window
```

### 3. Controls
Space → pause / resume.   
Esc → exit.   
Viewer automatically replays all events from events.bin.   

##  Repository Structure

```text
Map/
├─ include/map/
│  ├─ core/      (Event, EventBus, Logger)
│  ├─ book/      (OrderBook, Trade)
│  ├─ types/     (Price, Quantity, Notional)
│  └─ replay/    (LogReader)
│
├─ src/
│  ├─ core/      (Logger.cpp, Clock.cpp)
│  ├─ book/      (OrderBook.cpp)
│  ├─ replay/    (LogReader.cpp)
│  └─ apps/
│       ├─ live_sim.cpp
│       ├─ ob_viewer.cpp
│       └─ strategy_demo.cpp
│
├─ tests/        (unit + smoke tests)
├─ assets/       (DejaVuSans.ttf font, future visual assets)
├─ build/        (compiled binaries)
├─ CMakeLists.txt
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

 <ins> Run Tests </ins>
 ```bash
ctest --test-dir build --output-on-failure
```


 <ins> Run Demos </ins>
  ```bash
./build/map_hft_main     # console demo
./build/live_sim         # generate new events.bin
./build/ob_viewer        # visualize replay
```
 
 SFML Order Book Viewer     

What It Shows.    
Green → bids.    
Red → asks.   
Displays mid price and spread.   
Shows a moving sweep line over the price axis.   
Press Spacebar to inject random orders (and/or pause/resume, depending on build).   

Run after building:
  ```bash
./build/ob_viewer
```

### Continuous Integration (CI)

GitHub Actions workflow:   ```bash.github/workflows/ci.yml```

This workflow runs automatically on every push or pull request:    

1. Installs dependencies (CMake, SFML) <br>

2. Configures project using Ninja <br>

3. Builds all targets <br>

4. Runs all tests with ctest <br>

The CI badge will appear here once the workflow passes 
