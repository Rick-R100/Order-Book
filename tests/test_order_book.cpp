#include <cstdio>
#include <cstdlib>
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
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}