# MAP Engine Binary Log Format

The MAP engine writes all events to a binary log via `map::Logger`.
This document defines the on-disk layout used by `Logger` and `LogReader`.

All integers are written in **little-endian**.
The log is a contiguous sequence of **records**.  
There is no footer; the reader continues until EOF.

---

## General Layout

```
FileHeader
RecordHeader + EventPayload
RecordHeader + EventPayload
...
EOF
```

The header appears once at the top of the file.  
Each record begins with a type tag and a size field.

---

## File Header

```
char[4]   magic       = "MAP1"
uint8     version     = 1
uint8     reserved    = 0
uint16    header_size = 8
```

Meaning:

| Field       | Type     | Description                    |
|-------------|----------|--------------------------------|
| magic       | char[4]  | File identifier ("MAP1")       |
| version     | uint8    | Format version (1)             |
| reserved    | uint8    | Padding                        |
| header_size | uint16   | Size of this header (8 bytes)  |

---

## Record Header

Every event record begins with:

```
uint8     type
uint8     reserved
uint16    size
uint64    seq
int64     ts_ns
```

| Field    | Type     | Description                                |
|----------|----------|--------------------------------------------|
| type     | uint8    | Event type enum                            |
| reserved | uint8    | Always 0 (padding)                         |
| size     | uint16   | Size of the event payload in bytes         |
| seq      | uint64   | Monotonic sequence number (0,1,2,...)      |
| ts_ns    | int64    | Timestamp in nanoseconds                   |

### Event Type Enum

```
0 = NewOrderEvent
1 = CancelOrderEvent
2 = TradeEvent
```

---

## Payload Layouts

All payloads are fixed-size POD structs.
Strong typedefs are stored using their underlying integer type:

- Price → int64
- Quantity → int64
- Notional → int64
- Side → uint8 (`0 = Bid`, `1 = Ask`)
- OrderId → uint64
- symbol → char[8] (null padded)

---

## 1. NewOrderEvent (type = 0)

```
uint64   order_id
uint8    side
char[8]  symbol
int64    price
int64    qty
```

Represents a new limit order.

---

## 2. CancelOrderEvent (type = 1)

```
uint64   order_id
int64    qty
```

Represents a cancel (partial or full).

---

## 3. TradeEvent (type = 2)

```
uint64   maker_id
uint64   taker_id
int64    price
int64    qty
int64    notional
uint8    aggressor_side
```

Represents a matched trade between two orders.

---

## Determinism and Replay

Replay (`LogReader`) processes:

1. FileHeader
2. For each record:
    - Read RecordHeader
    - Read EventPayload
    - Dispatch to OrderBook via EventBus

If the log is identical, replay produces the same:

- order states
- trades
- checksum

---

## Example Commands

Build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run live:

```bash
./build/live_sim
```

Replay:

```bash
./build/replay_test
```

