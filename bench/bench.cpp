// Latency / throughput benchmark on a synthetic, seeded order flow.
// Usage: ./bench [num_ops] [seed]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "orderbook/order_book.hpp"

using namespace ob;
using Clock = std::chrono::steady_clock;

enum class Kind : std::uint8_t { Limit, Market, Cancel };

struct Op {
    Kind kind;
    Side side;
    Price price;
    Qty qty;
    OrderId id;
};

// Pre-generate the whole stream so RNG cost is not inside the timed region.
static std::vector<Op> generate(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<Op> ops;
    ops.reserve(n);
    const Price mid = 10000;  // ticks
    OrderId next_id = 1;

    for (std::size_t i = 0; i < n; ++i) {
        const int pick = static_cast<int>(rng() % 100);
        const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
        if (pick < 60) {  // passive-ish limit orders clustered around the mid
            const Price offset = static_cast<Price>(rng() % 50);
            const Price price = side == Side::Buy ? mid - offset : mid + offset;
            ops.push_back({Kind::Limit, side, price, 1 + rng() % 100, next_id++});
        } else if (pick < 70) {  // aggressive limit that crosses the spread
            const Price offset = static_cast<Price>(rng() % 10);
            const Price price = side == Side::Buy ? mid + offset : mid - offset;
            ops.push_back({Kind::Limit, side, price, 1 + rng() % 100, next_id++});
        } else if (pick < 80) {
            ops.push_back({Kind::Market, side, 0, 1 + rng() % 100, next_id++});
        } else {  // cancel a previously issued id (may already be filled -> returns false)
            // Bias toward recent ids so a healthy share of cancels hit live orders.
            const OrderId window = std::min<OrderId>(next_id > 1 ? next_id - 1 : 1, 20000);
            const OrderId target = next_id > 1 ? next_id - 1 - rng() % window : 1;
            ops.push_back({Kind::Cancel, side, 0, 0, target});
        }
    }
    return ops;
}

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 2'000'000;
    const std::uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 42;

    const std::vector<Op> ops = generate(n, seed);
    OrderBook book(n);  // pre-size the id index (see header)
    std::vector<Trade> trades;
    trades.reserve(1024);
    std::vector<std::uint32_t> latency_ns;
    latency_ns.reserve(n);
    std::size_t total_trades = 0;
    std::size_t cancels = 0, cancels_hit = 0;

    // Warm-up pass on a throwaway book (caches, branch predictor, allocator).
    {
        OrderBook warm;
        std::vector<Trade> sink;
        const std::size_t w = std::min<std::size_t>(n / 10, 100000);
        for (std::size_t i = 0; i < w; ++i) {
            const Op& op = ops[i];
            sink.clear();
            if (op.kind == Kind::Limit) warm.add_limit(op.id, op.side, op.price, op.qty, sink);
            else if (op.kind == Kind::Market) warm.add_market(op.id, op.side, op.qty, sink);
            else warm.cancel(op.id);
        }
    }

    const auto wall_start = Clock::now();
    for (const Op& op : ops) {
        trades.clear();
        const auto t0 = Clock::now();
        switch (op.kind) {
            case Kind::Limit: book.add_limit(op.id, op.side, op.price, op.qty, trades); break;
            case Kind::Market: book.add_market(op.id, op.side, op.qty, trades); break;
            case Kind::Cancel: cancels_hit += book.cancel(op.id); ++cancels; break;
        }
        const auto t1 = Clock::now();
        total_trades += trades.size();
        latency_ns.push_back(static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }
    const double wall_s = std::chrono::duration<double>(Clock::now() - wall_start).count();

    std::sort(latency_ns.begin(), latency_ns.end());
    auto pct = [&](double p) {
        return latency_ns[std::min(latency_ns.size() - 1,
                                   static_cast<std::size_t>(p * latency_ns.size()))];
    };

    std::printf("operations      : %zu (seed %llu)\n", n, static_cast<unsigned long long>(seed));
    std::printf("trades produced : %zu\n", total_trades);
    std::printf("resting orders  : %zu at end\n", book.order_count());
    std::printf("cancels         : %zu (%.1f%% hit a live order)\n", cancels,
                cancels ? 100.0 * cancels_hit / cancels : 0.0);
    std::printf("throughput      : %.2f M ops/s (wall clock, includes timer overhead)\n",
                n / wall_s / 1e6);
    std::printf("latency ns      : p50=%u p90=%u p99=%u p99.9=%u max=%u\n", pct(0.50), pct(0.90),
                pct(0.99), pct(0.999), latency_ns.back());
    return 0;
}