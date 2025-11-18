# Week 3 – Performance Notes

## Live Simulation (`live_sim --benchmark`)

The live simulation writes `perf_live.csv` with columns:

- `tick`: simulation tick (0–500)
- `orders`: cumulative number of `NewOrderEvent`s by that tick

On my machine, a benchmark run produced:

- Orders generated: **71**
- Elapsed time: **0.000170597 s**
- Throughput: **~416,186 orders/sec**
- Latency per event: **≈ 2,400 ns**

Live simulation work includes:

- EventBus routing
- Strategy logic with an intent engine (adaptive aggressiveness)
- OrderBook updates
- Logging *all* events to `events.bin`

This is the “full engine” path.

---

## Replay Performance (`replay_test`)

The replay executable reconstructs the OrderBook from `events.bin` and writes `perf_replay.csv`:


events,seconds,events_per_sec
71,8.8313e-05,803959
So:

Events replayed: 71

Elapsed time: 0.000088313 s

Replay throughput: ~803,959 events/sec

Replay is faster because:

No strategy logic or random generation

No intent engine

No logging, just reading and applying events

Only pure OrderBook reconstruction

Critically, live and replay end in the same checksum (e.g., 1831), which proves deterministic behavior.

Visualizations
Two plots were generated (in Colab) and stored in docs/images/:

Live Simulation Order Generation Over Time

X-axis: tick (0–500)

Y-axis: cumulative orders

Shows smooth, adaptive order generation as the strategy runs.

Replay Performance

A simple bar showing replay throughput in events/sec (~804k).

Demonstrates that replay is significantly faster than the live path while producing the same final book state.

These plots, plus matching checksums, are what we use in the final report/demo to argue:

The MAP engine supports deterministic replay with measured, high-throughput performance on both the live and replay code paths.

