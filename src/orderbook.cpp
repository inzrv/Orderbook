#include "orderbook.h"

#include <format>

std::vector<Trade> Orderbook::add(std::shared_ptr<Order> order)
{
    if (!order) {
        return {};
    }

    if (m_orders.contains(order->id)) {
        return {};
    }

    if (order->side == Side::UNKNOWN) {
        throw std::logic_error(std::format("Order ({}) cannot be added to the orderbook.", order->id));
    }

    if (order->type == Order::Type::MAR) {
        auto gtc_order = processMAR(order);
        if (!gtc_order) {
            return {};
        }

        order = gtc_order;
    }

    if (order->type == Order::Type::FAK && !canMatch(order->side, order->price)) {
        return {};
    }

    std::list<std::shared_ptr<Order>>::iterator location;

    if (order->side == Side::BUY) {
        auto& level = m_bids[order->price];
        level.push_back(order);
        location = std::prev(level.end());
    } else {
        auto& level = m_asks[order->price];
        level.push_back(order);
        location = std::prev(level.end());
    }

    m_orders.emplace(order->id, OrderEntry{order, location});

    const auto trades = match();
    return trades;
}

void Orderbook::cancel(Order::Id order_id)
{
    if (!m_orders.contains(order_id)) {
        return;
    }

    const auto [order, location] = m_orders[order_id];
    m_orders.erase(order_id);

    const auto price = order->price;
    if (order->side == Side::BUY) {
        auto& level = m_bids.at(price);
        level.erase(location);
        if (level.empty()) {
            m_bids.erase(price);
        }
    } else if (order->side == Side::SELL) {
        auto& level = m_asks.at(price);
        level.erase(location);
        if (level.empty()) {
            m_asks.erase(price);
        }
    }
}

std::vector<Trade> Orderbook::modify(Order::Id order_id, const Change& change)
{
    if (m_orders.contains(order_id)) {
        return {};
    }

    auto [order, location] = m_orders[order_id];

    auto new_order = Order {
        .id = order_id,
        .type = order->type,
        .remainder = change.remainder,
        .side = change.side,
        .price = change.price
    };

    cancel(order_id);
    const auto trades = add(std::make_shared<Order>(new_order));
    return trades;
}

std::vector<Trade> Orderbook::match()
{
    std::vector<Trade> trades;

    while (!m_asks.empty() && !m_bids.empty()) {
        const auto& [best_bid, bids] = *m_bids.begin();
        const auto& [best_ask, asks] = *m_asks.begin();

        if (best_ask > best_bid) {
            break;
        }

        while (!bids.empty() && !asks.empty()) {
            const auto trade = matchTop();
            trades.push_back(std::move(trade));
        }

        if (bids.empty()) {
            m_bids.erase(best_bid);
        }

        if (asks.empty()) {
            m_asks.erase(best_ask);
        }
    }

    cancelFAKs();

    return trades;
}

Trade Orderbook::matchTop()
{
    auto& [_, bids] = *m_bids.begin();
    auto& [_, asks] = *m_asks.begin();

    auto bid = bids.front();
    auto ask = asks.front();

    const auto quantity = std::min(bid->remainder, ask->remainder);

    bid->fill(quantity);
    if (bid->filled()) {
        bids.pop_front();
        m_orders.erase(bid->id);
    }

    ask->fill(quantity);
    if (ask->filled()) {
        asks.pop_front();
        m_orders.erase(ask->id);
    }

    Trade trade = {
        .bid_info = {
            .order_id = bid->id,
            .price = bid->price,
            .quantity = quantity
        },
        .ask_info = {
            .order_id = ask->id,
            .price = ask->price,
            .quantity = quantity
        }
    };

    return trade;
}

void Orderbook::cancelFAKs()
{
    if (!m_bids.empty()) {
        auto& [_, bids] = *m_bids.begin();
        auto bid = bids.front();
        if (bid->type == Order::Type::FAK) {
            cancel(bid->id);
        }
    }

    if (!m_asks.empty()) {
        auto& [_, asks] = *m_asks.begin();
        auto ask = asks.front();
        if (ask->type == Order::Type::FAK) {
            cancel(ask->id);
        }
    }
}

std::shared_ptr<Order> Orderbook::processMAR(std::shared_ptr<Order> order) const
{
    if (!order || order->type != Order::Type::MAR) {
        return nullptr;
    }

    Price worst_price{0};

    if (order->side == Side::BUY && !m_asks.empty()) {
        const auto& [worst_ask, _] = *m_asks.rbegin();
        worst_price = worst_ask;
    } else if (order->side == Side::SELL && !m_bids.empty()) {
        const auto& [worst_bid, _] = *m_bids.rbegin();
        worst_price = worst_bid;
    } else {
        return nullptr;
    }

    auto gtc_order = std::make_shared<Order>(*order);
    gtc_order->type = Order::Type::GTC;
    gtc_order->price = worst_price;

    return gtc_order;
}

bool Orderbook::canMatch(Side side, Price price) const
{
    if (side == Side::BUY) {
        if (m_asks.empty()) {
            return false;
        }

        const auto& [best_ask, _] = *m_asks.begin();
        return best_ask <= price;
    }

    if (side == Side::SELL) {
        if (m_bids.empty()) {
            return false;
        }

        const auto& [best_bid, _] = *m_bids.begin();
        return best_bid >= price;
    }

    return false;
}
