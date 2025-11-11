# MAP Engine Binary Log Format

The engine writes all events to a binary log via `map::Logger`.  
This file documents the on-disk layout used by `Logger` and `LogReader`.

## General Layout

The log is a contiguous sequence of **records**.  
Each record begins with a 1-byte type tag:

- `0x01` – `NewOrderEvent`
- `0x02` – `CancelOrderEvent`
- `0x03` – `TradeEvent`

There is currently no header or footer; the file can be read until EOF.

All integers are written in **little-endian**.

## Encodings

### Strings

Strings are encoded as:

- `uint32` length in bytes
- raw UTF-8 bytes (no null terminator)

### Integers

We use strong typedefs in code (`Price`, `Quantity`, `OrderId`) but on disk:

- `Price`    → `int64`
- `Quantity` → `int64`
- `OrderId`  → `uint64`
- `Side`     → `int32` (enum value cast)

## Record Layouts

### 1. NewOrderEvent (`0x01`)

```text
u8      type = 1
u32     symbol_length
char[]  symbol bytes
i64     price      (Price::raw())
i64     qty        (Quantity::raw())
i32     side       (static_cast<int32_t>(Side))
```
### 2. CancelOrderEvent (0x02)
```text
u8      type = 2
u32     symbol_length
char[]  symbol bytes
u64     order_id   (OrderId::raw())
```

### 3.. TradeEvent (0x03)
```text
   u8      type = 3
   u32     symbol_length
   char[]  symbol bytes
   u64     taker_id   (OrderId::raw())
   u64     maker_id   (OrderId::raw())
   i64     price      (Price::raw())
   i64     qty        (Quantity::raw())
   i32     taker_side (static_cast<int32_t>(Side))
```

---

## 4. After adding everything – build & test

From the project root:

```bash
cd /Users/avimaslow/CLionProjects/Map

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMAP_BUILD_OB_VIEWER=OFF
cmake --build build

# run unit tests (includes replay test)
ctest --test-dir build --output-on-failure

# run live sim + replay manually
./build/live_sim
./build/replay_test

