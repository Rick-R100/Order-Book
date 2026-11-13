# C++ Limit Order Book & Matching Engine

[![CI](https://github.com/Rick-R100/Order-Book/actions/workflows/ci.yml/badge.svg)](https://github.com/Rick-R100/Order-Book/actions/workflows/ci.yml)

A single-instrument limit order book with **price-time priority** matching, written in C++17 with no external dependencies. It supports limit orders, market orders, cancels and modifies, and ships with a randomized differential test and a latency benchmark.

## Features

- Limit orders (match while the price crosses, rest the remainder) and market orders (remainder is discarded)
- Cancel and modify (reduce-size keeps queue priority; any other change loses it, as on most exchanges)
- Trades always execute at the **resting order's price**
- Integer tick prices (no floating-point comparisons)
- Rejects zero-quantity orders and duplicate live order ids

## Design

| Piece | Structure | Why |
|---|---|---|
| Bids / asks | `std::map<Price, Level>` (bids sorted descending) | Best price is always `begin()`; O(log P) insert/erase where P = number of price levels |
| Orders at one price | `std::list<Order>` per level | FIFO queue gives time priority; erasing a node by iterator is O(1) |
| Order lookup | `unordered_map<OrderId, {side, price, list iterator}>` | O(1) cancel/modify without scanning the book |

Complexity per operation:

- **Cancel**: O(1) lookup + O(1) list erase (+ O(log P) only if the level empties)
- **Add (no match)**: O(log P)
- **Add (matching)**: O(log P + k) for k orders consumed

## Build & test

With CMake:

```bash
cmake -S . -B build          # defaults to a Release build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/bench                # defaults: 2,000,000 ops, seed 42
```

Or directly with g++:

```bash
g++ -std=c++17 -O2 -Iinclude src/order_book.cpp tests/test_order_book.cpp -o test && ./test
g++ -std=c++17 -O2 -Iinclude src/order_book.cpp bench/bench.cpp -o bench && ./bench
```

## Testing

- **Unit tests** cover FIFO priority, price priority across levels, partial fills, market orders on an empty book, cancel/modify semantics, and input rejection.
- **Differential test**: 20 seeds x 20,000 random operations (add, market, cancel, modify) in a tight price band so matches are frequent. Every trade and the final book state are compared against a deliberately naive O(n) reference implementation. This catches bugs that the hand-written tests miss: changing `modify`'s `new_qty < qty` check to `<=` still passes all unit tests but fails the differential test.
- CI builds and runs the tests with both GCC and Clang.

## Benchmark

Synthetic, seeded flow: 60% passive limits near the mid, 10% aggressive limits, 10% market orders, 20% cancels. Operations are pre-generated so RNG cost is not timed; each operation is timed individually with `steady_clock`.

**Machine:** AMD Ryzen 5 Microsoft Surface (R) Edition, Windows 11, g++ 15.2.0 (MSYS2 UCRT64), `-std=c++17 -O2`. 2,000,000 operations, seed 42. Median of 4 runs, with the range in parentheses.

| Metric | Median (range) |
|---|---|
| Throughput | 4.3 M ops/s (3.4 - 4.8) |
| Latency p50 | ~100 ns (100 - 200) |
| Latency p99 | ~0.8 us (0.7 - 1.0) |
| Latency p99.9 | ~2.4 us (1.8 - 2.6) |

Notes on reading these numbers:

- `steady_clock` ticks in 100 ns steps on Windows, so latencies are quantized to multiples of about 100 ns. Read p50 as an approximate figure.
- Each timed region includes the cost of two `steady_clock::now()` calls, so small latencies are slightly overstated.
- Max latency is dominated by OS scheduling jitter, so p99 / p99.9 are the meaningful tail figures.
- The id index is pre-sized (`OrderBook(expected_orders)`). Without it, hash-table rehashing produces rare spikes of many milliseconds, which is why the constructor takes a capacity hint.
- The synthetic flow is not real market data. The book grows during the run because adds outnumber cancels and fills, which makes this a deliberately deep-book stress case.

## Limitations and next steps

- `std::map` and `std::list` allocate per node and chase pointers. A production engine would use a flat array indexed by price tick, intrusive lists and a pooled allocator.
- Single-threaded, single instrument.
- No self-trade prevention, order types beyond limit/market (IOC, FOK, post-only), or stop orders.
- `volume_at` is O(orders at the level); a per-level cached total would make depth snapshots O(1) per level.

## Layout

```
include/orderbook/order_book.hpp   public API
src/order_book.cpp                 matching engine
tests/test_order_book.cpp          unit + differential tests
bench/bench.cpp                    latency / throughput benchmark
```

## License

MIT