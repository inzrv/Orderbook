#include "orderbook.h"

#include <chrono>
#include <format>

Orderbook::Orderbook() : m_orders_prune_thread{[this] { pruneGFD(); }} {}

Orderbook::~Orderbook()
{
    m_shutdown.store(true, std::memory_order_release);
	m_shutdown_cv.notify_one();
	m_orders_prune_thread.join();
}

void Orderbook::pruneGFD()
{
    while (true) {
        const auto next = nextPruneTime();

        {
            std::unique_lock lock{m_orders_mutex};
            m_shutdown_cv.wait_until(lock, next, [&]() { return m_shutdown.load(std::memory_order_acquire); });
            if (m_shutdown.load(std::memory_order_acquire)) {
                return;
            }
        }

        std::vector<Order::Id> ids;
        {
            std::scoped_lock lock{m_orders_mutex};
    
            for (const auto& [_, entry] : m_orders) {
                const auto& [order, _] = entry;
                if (order->type == Order::Type::GFD) {
                    ids.push_back(order->id);
                }
            }
        }

        if (!ids.empty()) {
            cancel(ids);
        }
    }
}

std::chrono::system_clock::time_point Orderbook::nextPruneTime() const
{
    using namespace std::chrono;
    const auto now_c = system_clock::to_time_t(system_clock::now());

    std::tm t{};
    localtime_r(&now_c, &t);

    if (t.tm_hour >= kPruneHour) {
        t.tm_mday += 1;
    }

    t.tm_hour = kPruneHour;
    t.tm_min = 0;
    t.tm_sec = 0;

    return system_clock::from_time_t(mktime(&t));
}

std::vector<Trade> Orderbook::add(std::shared_ptr<Order> order)
{
    std::scoped_lock lock{m_orders_mutex};
    return addImpl(order);
}

std::vector<Trade> Orderbook::addImpl(std::shared_ptr<Order> order)
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

    if (order->type == Order::Type::FOK && !canFullyFill(order->side, order->price, order->remainder)) {
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

    onAdd(order);

    const auto trades = match();
    return trades;
}

void Orderbook::onAdd(std::shared_ptr<Order> order)
{
    updateAggregatedLevel(order->side, order->price, order->remainder, Action::ADD);
}

void Orderbook::cancel(const std::vector<Order::Id>& order_ids)
{
    std::scoped_lock lock{m_orders_mutex};
    for (const auto& id : order_ids) {
        cancelImpl(id);
    }
}

void Orderbook::cancel(Order::Id order_id)
{
    std::scoped_lock lock{m_orders_mutex};
    cancelImpl(order_id);
}

void Orderbook::cancelImpl(Order::Id order_id)
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

    onCancel(order);
}

void Orderbook::onCancel(std::shared_ptr<Order> order)
{
    updateAggregatedLevel(order->side, order->price, order->remainder, Action::REMOVE);
}

std::vector<Trade> Orderbook::modify(Order::Id order_id, const Change& change)
{
    std::scoped_lock lock{m_orders_mutex};

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

    cancelImpl(order_id);

    const auto trades = addImpl(std::make_shared<Order>(new_order));
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

    onMatch(Side::BUY, bid->price, quantity, bid->filled());
    onMatch(Side::SELL, ask->price, quantity, ask->filled());

    return trade;
}

void Orderbook::onMatch(Side side, Price price, Quantity quantity, bool is_fully_filled)
{
    const auto action = is_fully_filled ? Action::REMOVE : Action::MATCH;
    updateAggregatedLevel(side, price, quantity, action);
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

void Orderbook::updateAggregatedLevel(Side side, Price price, Quantity quantity, Action action)
{
    if (side == Side::UNKNOWN) {
        return;
    }

    auto& agg_level = (side == Side::BUY) ? m_aggregated_bids[price] : m_aggregated_asks[price];

    if (action == Action::ADD) {
        ++agg_level.count;
        agg_level.quantity += quantity;
    } else if (action == Action::REMOVE) {
        --agg_level.count;
        agg_level.quantity -= quantity;
    } else if (action == Action::MATCH) {
        agg_level.quantity -= quantity;
    }

    if (agg_level.count > 0) {
        return;
    }

    if (side == Side::BUY) {
        m_aggregated_bids.erase(price);
    } else {
        m_aggregated_asks.erase(price);
    }
}

bool Orderbook::canFullyFill(Side side, Price price, Quantity quantity) const
{
    if (side == Side::UNKNOWN || !canMatch(side, price)) {
        return false;
    }

    if (side == Side::BUY) {
        return canFullyFillBid(price, quantity);
    } else {
        return canFullyFillAsk(price, quantity);
    }
}

bool Orderbook::canFullyFillBid(Price price, Quantity quantity) const
{
    if (quantity == 0) {
        return true;
    }

    for (const auto& [ask_price, agg_level] : m_aggregated_asks) {
        if (ask_price > price) {
            return false;
        }

        if (agg_level.quantity >= quantity) {
            return true;
        }

        quantity -= agg_level.quantity;
    }

    return false;
}

bool Orderbook::canFullyFillAsk(Price price, Quantity quantity) const
{
    if (quantity == 0) {
        return true;
    }

    for (const auto& [bid_price, agg_level] : m_aggregated_bids) {
        if (bid_price < price) {
            return false;
        }

        if (agg_level.quantity >= quantity) {
            return true;
        }

        quantity -= agg_level.quantity;
    }

    return false;
}
