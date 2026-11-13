#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ob {

using OrderId = std::uint64_t;
using Price = std::int64_t;  // integer ticks; avoids floating-point comparison bugs
using Qty = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

struct Trade {
    OrderId taker_id;
    OrderId maker_id;
    Price price;  // always the resting (maker) order's price
    Qty qty;
    Side taker_side;
};

// Single-instrument limit order book with price-time priority.
//
// All operations append any resulting trades to `out`. The book never clears
// `out`, so callers can reuse one buffer across calls without reallocating.
class OrderBook {
public:
    // `expected_orders` pre-sizes the id index. Without it, the hash table
    // rehashes as it grows, which shows up as rare multi-millisecond latency spikes.
    explicit OrderBook(std::size_t expected_orders = 0) { index_.reserve(expected_orders); }

    // Limit order: matches against the opposite side while prices cross, then
    // rests any remainder. Returns false (and does nothing) if qty == 0 or
    // the id is already live.
    bool add_limit(OrderId id, Side side, Price price, Qty qty, std::vector<Trade>& out);

    // Market order: matches at any price until filled or the book is empty.
    // Unfilled remainder is discarded (it never rests). Returns false if qty == 0.
    bool add_market(OrderId id, Side side, Qty qty, std::vector<Trade>& out);

    // Removes a resting order. Returns false if the id is not live.
    bool cancel(OrderId id);

    // Modify a resting order.
    //  - new_qty == 0                         -> cancel
    //  - same price and new_qty < current qty -> reduce in place, keeps queue priority
    //  - anything else                        -> cancel + re-add (same id), loses priority,
    //                                            and may match if the new price crosses
    // Returns false if the id is not live.
    bool modify(OrderId id, Price new_price, Qty new_qty, std::vector<Trade>& out);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Qty volume_at(Side side, Price price) const;  // O(orders at that level)
    std::size_t order_count() const { return index_.size(); }
    bool contains(OrderId id) const { return index_.count(id) != 0; }

private:
    struct Order {
        OrderId id;
        Qty qty;
    };
    using Level = std::list<Order>;  // FIFO queue: front = oldest = highest priority

    struct Locator {
        Side side;
        Price price;
        Level::iterator it;
    };

    // Bids sorted high -> low, asks low -> high, so begin() is always the best level.
    std::map<Price, Level, std::greater<Price>> bids_;
    std::map<Price, Level, std::less<Price>> asks_;
    // O(1) lookup of any resting order for cancel/modify.
    std::unordered_map<OrderId, Locator> index_;

    template <class BookSide, class Crosses>
    void match(BookSide& book, Crosses crosses, OrderId taker, Side taker_side, Qty& remaining,
               std::vector<Trade>& out);

    void rest(OrderId id, Side side, Price price, Qty qty);
};

}  // namespace ob