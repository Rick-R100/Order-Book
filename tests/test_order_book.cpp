#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "orderbook/order_book.hpp"

using namespace ob;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static void test_resting_and_best_prices() {
    OrderBook book;
    std::vector<Trade> out;
    CHECK(!book.best_bid() && !book.best_ask());
    CHECK(book.add_limit(1, Side::Buy, 100, 10, out));
    CHECK(book.add_limit(2, Side::Sell, 105, 5, out));
    CHECK(out.empty());
    CHECK(book.best_bid() == 100);
    CHECK(book.best_ask() == 105);
    CHECK(book.order_count() == 2);
    CHECK(book.volume_at(Side::Buy, 100) == 10);
}
static bool same_trade(const Trade& a, const Trade& b) {
    return a.taker_id == b.taker_id && a.maker_id == b.maker_id && a.price == b.price &&
           a.qty == b.qty && a.taker_side == b.taker_side;
}

static void test_full_fill_at_maker_price() {
    OrderBook book;
    std::vector<Trade> out;
    book.add_limit(1, Side::Sell, 100, 10, out);
    book.add_limit(2, Side::Buy, 105, 10, out);  // willing to pay 105, trades at 100
    CHECK(out.size() == 1);
    CHECK(same_trade(out[0], Trade{2, 1, 100, 10, Side::Buy}));
    CHECK(book.order_count() == 0);
    CHECK(!book.best_ask() && !book.best_bid());
}

static void test_partial_fill_rests_remainder() {
    OrderBook book;
    std::vector<Trade> out;
    book.add_limit(1, Side::Sell, 100, 4, out);
    book.add_limit(2, Side::Buy, 100, 10, out);
    CHECK(out.size() == 1 && out[0].qty == 4);
    CHECK(book.best_bid() == 100);
    CHECK(book.volume_at(Side::Buy, 100) == 6);
    CHECK(!book.best_ask());
}

static void test_time_priority_fifo() {
    OrderBook book;
    std::vector<Trade> out;
    book.add_limit(1, Side::Sell, 100, 5, out);
    book.add_limit(2, Side::Sell, 100, 5, out);
    book.add_limit(3, Side::Sell, 100, 5, out);
    book.add_limit(9, Side::Buy, 100, 7, out);
    CHECK(out.size() == 2);
    CHECK(out[0].maker_id == 1 && out[0].qty == 5);
    CHECK(out[1].maker_id == 2 && out[1].qty == 2);
    CHECK(book.volume_at(Side::Sell, 100) == 8);
}

static void test_price_priority_across_levels() {
    OrderBook book;
    std::vector<Trade> out;
    book.add_limit(1, Side::Sell, 102, 5, out);
    book.add_limit(2, Side::Sell, 100, 5, out);
    book.add_limit(3, Side::Sell, 101, 5, out);
    book.add_limit(9, Side::Buy, 102, 12, out);  // sweeps 100, 101, then part of 102
    CHECK(out.size() == 3);
    CHECK(out[0].price == 100 && out[0].qty == 5);
    CHECK(out[1].price == 101 && out[1].qty == 5);
    CHECK(out[2].price == 102 && out[2].qty == 2);
    CHECK(book.volume_at(Side::Sell, 102) == 3);
}

static void test_limit_does_not_cross_beyond_limit_price() {
    OrderBook book;
    std::vector<Trade> out;
    book.add_limit(1, Side::Sell, 101, 5, out);
    book.add_limit(2, Side::Buy, 100, 5, out);
    CHECK(out.empty());
    CHECK(book.best_bid() == 100 && book.best_ask() == 101);
}

static void test_sell_side_matching() {
    OrderBook book;
    std::vector<Trade> out;
    book.add_limit(1, Side::Buy, 100, 5, out);
    book.add_limit(2, Side::Buy, 99, 5, out);
    book.add_limit(3, Side::Sell, 99, 8, out);
    CHECK(out.size() == 2);
    CHECK(out[0].maker_id == 1 && out[0].price == 100 && out[0].qty == 5);
    CHECK(out[1].maker_id == 2 && out[1].price == 99 && out[1].qty == 3);
    CHECK(out[0].taker_side == Side::Sell);
}
static void test_market_order_empty_book_and_remainder_dropped() {
    OrderBook book;
    std::vector<Trade> out;
    CHECK(book.add_market(1, Side::Buy, 10, out));
    CHECK(out.empty());
    book.add_limit(2, Side::Sell, 100, 3, out);
    book.add_market(3, Side::Buy, 10, out);
    CHECK(out.size() == 1 && out[0].qty == 3);
    CHECK(book.order_count() == 0);  // the unfilled 7 did not rest
}

static void test_cancel() {
    OrderBook book;
    std::vector<Trade> out;
    book.add_limit(1, Side::Buy, 100, 5, out);
    book.add_limit(2, Side::Buy, 100, 5, out);
    CHECK(book.cancel(1));
    CHECK(!book.cancel(1));    // already gone
    CHECK(!book.cancel(999));  // never existed
    CHECK(book.volume_at(Side::Buy, 100) == 5);
    CHECK(book.cancel(2));
    CHECK(!book.best_bid());  // empty level removed
    CHECK(book.order_count() == 0);
}

static void test_rejects_bad_input() {
    OrderBook book;
    std::vector<Trade> out;
    CHECK(!book.add_limit(1, Side::Buy, 100, 0, out));
    CHECK(book.add_limit(1, Side::Buy, 100, 5, out));
    CHECK(!book.add_limit(1, Side::Sell, 200, 5, out));  // duplicate live id
    CHECK(!book.add_market(2, Side::Buy, 0, out));
    CHECK(book.order_count() == 1);
}

static void test_modify_keeps_priority_only_on_size_decrease() {
    OrderBook book;
    std::vector<Trade> out;
    book.add_limit(1, Side::Sell, 100, 10, out);
    book.add_limit(2, Side::Sell, 100, 10, out);

    CHECK(book.modify(1, 100, 4, out));  // reduce: still first in queue
    book.add_market(9, Side::Buy, 1, out);
    CHECK(out.size() == 1 && out[0].maker_id == 1);

    out.clear();
    CHECK(book.modify(1, 100, 20, out));  // increase: goes to the back
    book.add_market(10, Side::Buy, 1, out);
    CHECK(out.size() == 1 && out[0].maker_id == 2);
}

static void test_modify_price_change_can_match_and_cancel() {
    OrderBook book;
    std::vector<Trade> out;
    book.add_limit(1, Side::Sell, 105, 5, out);
    book.add_limit(2, Side::Buy, 100, 5, out);
    CHECK(book.modify(2, 105, 5, out));  // now crosses
    CHECK(out.size() == 1 && out[0].taker_id == 2 && out[0].maker_id == 1);
    CHECK(book.order_count() == 0);

    book.add_limit(3, Side::Buy, 90, 5, out);
    CHECK(book.modify(3, 90, 0, out));  // qty 0 == cancel
    CHECK(!book.contains(3));
    CHECK(!book.modify(3, 90, 5, out));  // unknown id
}
// ------------------------------------------------- differential (random) test

namespace {

struct RefOrder {
    OrderId id;
    Side side;
    Price price;
    Qty qty;
    std::uint64_t seq;
};

// Obviously-correct, O(n) per match. If the fast engine and this agree across
// hundreds of thousands of random operations, the matching logic is sound.
class ReferenceBook {
public:
    bool add_limit(OrderId id, Side side, Price price, Qty qty, std::vector<Trade>& out) {
        if (qty == 0 || find(id) != orders_.end()) return false;
        Qty remaining = qty;
        match(id, side, true, price, remaining, out);
        if (remaining > 0) orders_.push_back({id, side, price, remaining, seq_++});
        return true;
    }
    bool add_market(OrderId id, Side side, Qty qty, std::vector<Trade>& out) {
        if (qty == 0) return false;
        Qty remaining = qty;
        match(id, side, false, 0, remaining, out);
        return true;
    }
    bool cancel(OrderId id) {
        auto it = find(id);
        if (it == orders_.end()) return false;
        orders_.erase(it);
        return true;
    }
    bool modify(OrderId id, Price new_price, Qty new_qty, std::vector<Trade>& out) {
        auto it = find(id);
        if (it == orders_.end()) return false;
        if (new_qty == 0) return cancel(id);
        if (new_price == it->price && new_qty < it->qty) {
            it->qty = new_qty;
            return true;
        }
        const Side side = it->side;
        orders_.erase(it);
        return add_limit(id, side, new_price, new_qty, out);
    }
    std::size_t size() const { return orders_.size(); }
    Qty volume_at(Side side, Price price) const {
        Qty total = 0;
        for (const auto& o : orders_)
            if (o.side == side && o.price == price) total += o.qty;
        return total;
    }

private:
    std::vector<RefOrder> orders_;
    std::uint64_t seq_ = 0;

    std::vector<RefOrder>::iterator find(OrderId id) {
        return std::find_if(orders_.begin(), orders_.end(),
                            [id](const RefOrder& o) { return o.id == id; });
    }

    void match(OrderId taker, Side side, bool has_limit, Price limit, Qty& remaining,
               std::vector<Trade>& out) {
        while (remaining > 0) {
            auto best = orders_.end();
            for (auto it = orders_.begin(); it != orders_.end(); ++it) {
                if (it->side == side) continue;
                if (has_limit && (side == Side::Buy ? it->price > limit : it->price < limit))
                    continue;
                if (best == orders_.end()) {
                    best = it;
                    continue;
                }
                const bool better_price =
                    side == Side::Buy ? it->price < best->price : it->price > best->price;
                const bool tie_earlier = it->price == best->price && it->seq < best->seq;
                if (better_price || tie_earlier) best = it;
            }
            if (best == orders_.end()) return;
            const Qty traded = std::min(remaining, best->qty);
            out.push_back(Trade{taker, best->id, best->price, traded, side});
            remaining -= traded;
            best->qty -= traded;
            if (best->qty == 0) orders_.erase(best);
        }
    }
};

}  // namespace

static void test_differential(std::uint64_t seed, int ops) {
    std::mt19937_64 rng(seed);
    OrderBook fast;
    ReferenceBook ref;
    std::vector<Trade> out_fast, out_ref;
    std::vector<OrderId> issued;
    OrderId next_id = 1;
    bool all_match = true;

    for (int i = 0; i < ops && all_match; ++i) {
        out_fast.clear();
        out_ref.clear();
        const int pick = static_cast<int>(rng() % 100);
        const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
        const Price price = 95 + static_cast<Price>(rng() % 11);  // tight band => many crosses
        const Qty qty = 1 + rng() % 10;
        bool ok_fast = false, ok_ref = false;

        if (pick < 50) {
            const OrderId id = next_id++;
            issued.push_back(id);
            ok_fast = fast.add_limit(id, side, price, qty, out_fast);
            ok_ref = ref.add_limit(id, side, price, qty, out_ref);
        } else if (pick < 60) {
            const OrderId id = next_id++;
            ok_fast = fast.add_market(id, side, qty, out_fast);
            ok_ref = ref.add_market(id, side, qty, out_ref);
        } else if (pick < 85 && !issued.empty()) {
            const OrderId id = issued[rng() % issued.size()];  // may already be filled
            ok_fast = fast.cancel(id);
            ok_ref = ref.cancel(id);
        } else if (!issued.empty()) {
            const OrderId id = issued[rng() % issued.size()];
            const Qty new_qty = rng() % 12;  // includes 0 => cancel
            ok_fast = fast.modify(id, price, new_qty, out_fast);
            ok_ref = ref.modify(id, price, new_qty, out_ref);
        } else {
            continue;
        }

        all_match = ok_fast == ok_ref && out_fast.size() == out_ref.size();
        for (std::size_t t = 0; all_match && t < out_fast.size(); ++t)
            all_match = same_trade(out_fast[t], out_ref[t]);
        all_match = all_match && fast.order_count() == ref.size();
    }
    CHECK(all_match);

    for (Price p = 95; p <= 105; ++p) {
        CHECK(fast.volume_at(Side::Buy, p) == ref.volume_at(Side::Buy, p));
        CHECK(fast.volume_at(Side::Sell, p) == ref.volume_at(Side::Sell, p));
    }
    const auto bb = fast.best_bid();
    const auto ba = fast.best_ask();
    if (bb && ba) CHECK(*bb < *ba);  // a resting book must never be crossed
}
int main() 
{
    test_resting_and_best_prices();
    test_full_fill_at_maker_price();
    test_partial_fill_rests_remainder();
    test_time_priority_fifo();
    test_price_priority_across_levels();
    test_limit_does_not_cross_beyond_limit_price();
    test_sell_side_matching();
    test_market_order_empty_book_and_remainder_dropped();
    test_cancel();
    test_rejects_bad_input();
    test_modify_keeps_priority_only_on_size_decrease();
    test_modify_price_change_can_match_and_cancel();
    for (std::uint64_t seed = 1; seed <= 20; ++seed) test_differential(seed, 20000);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}