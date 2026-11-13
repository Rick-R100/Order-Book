#include "orderbook/order_book.hpp"
#include <algorithm>
#include <iterator>

namespace ob {
template <class BookSide, class Crosses>
void OrderBook::match(BookSide& book, Crosses crosses, OrderId taker, Side taker_side,
                      Qty& remaining, std::vector<Trade>& out) {
    while (remaining > 0 && !book.empty()) {
        auto level_it = book.begin();  
        const Price level_price = level_it->first;
        if (!crosses(level_price)) break;

        Level& queue = level_it->second;
        while (remaining > 0 && !queue.empty()) {
            Order& maker = queue.front(); 
            const Qty traded = std::min(remaining, maker.qty);
            out.push_back(Trade{taker, maker.id, level_price, traded, taker_side});
            maker.qty -= traded;
            remaining -= traded;
            if (maker.qty == 0) {
                index_.erase(maker.id);
                queue.pop_front();
            }
        }
        if (queue.empty()) book.erase(level_it);
    }
}
void OrderBook::rest(OrderId id, Side side, Price price, Qty qty) {
    if (side == Side::Buy) {
        Level& queue = bids_[price];
        queue.push_back(Order{id, qty});
        index_[id] = Locator{side, price, std::prev(queue.end())};
    } else {
        Level& queue = asks_[price];
        queue.push_back(Order{id, qty});
        index_[id] = Locator{side, price, std::prev(queue.end())};
    }
}

bool OrderBook::add_limit(OrderId id, Side side, Price price, Qty qty, std::vector<Trade>& out) {
    if (qty == 0 || index_.count(id)) return false;

    Qty remaining = qty;
    if (side == Side::Buy) {
        match(asks_, [price](Price ask) { return ask <= price; }, id, side, remaining, out);
    } else {
        match(bids_, [price](Price bid) { return bid >= price; }, id, side, remaining, out);
    }
    if (remaining > 0) rest(id, side, price, remaining);
    return true;
}
bool OrderBook::add_market(OrderId id, Side side, Qty qty, std::vector<Trade>& out) {
    if (qty == 0) return false;

    Qty remaining = qty;
    auto always = [](Price) { return true; };
    if (side == Side::Buy) {
        match(asks_, always, id, side, remaining, out);
    } else {
        match(bids_, always, id, side, remaining, out);
    }
    return true;  // any remainder is dropped by design
}

bool OrderBook::cancel(OrderId id) {
    auto found = index_.find(id);
    if (found == index_.end()) return false;

    const Locator loc = found->second;
    if (loc.side == Side::Buy) {
        auto level_it = bids_.find(loc.price);
        level_it->second.erase(loc.it);
        if (level_it->second.empty()) bids_.erase(level_it);
    } else {
        auto level_it = asks_.find(loc.price);
        level_it->second.erase(loc.it);
        if (level_it->second.empty()) asks_.erase(level_it);
    }
    index_.erase(found);
    return true;
}

bool OrderBook::modify(OrderId id, Price new_price, Qty new_qty, std::vector<Trade>& out) {
    auto found = index_.find(id);
    if (found == index_.end()) return false;

    const Locator loc = found->second;
    if (new_qty == 0) return cancel(id);

    if (new_price == loc.price && new_qty < loc.it->qty) {
        loc.it->qty = new_qty;  // size decrease: keep place in the queue
        return true;
    }

    const Side side = loc.side;
    cancel(id);
    return add_limit(id, side, new_price, new_qty, out);
}
std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

Qty OrderBook::volume_at(Side side, Price price) const {
    Qty total = 0;
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end()) for (const Order& o : it->second) total += o.qty;
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end()) for (const Order& o : it->second) total += o.qty;
    }
    return total;
}

}  // namespace ob