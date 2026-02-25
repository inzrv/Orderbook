#pragma once

#include "types/order.h"
#include "types/change.h"
#include "types/trade.h"

#include <map>
#include <memory>
#include <list>

class Orderbook
{
public:
    struct OrderEntry
    {
        std::shared_ptr<Order> order{nullptr};
        std::list<std::shared_ptr<Order>>::iterator location{};
    };

    std::vector<Trade> add(std::shared_ptr<Order> order);
    void cancel(Order::Id order_id);
    std::vector<Trade> modify(Order::Id order_id, const Change& change);

private:
    std::vector<Trade> match();
    Trade matchTop();
    bool canMatch(Side side, Price price) const;
    void cancelFAKs();
    std::shared_ptr<Order> processMAR(std::shared_ptr<Order> order) const;
    std::optional<Price> bestPrice(Side side) const;

private:
    std::unordered_map<Order::Id, OrderEntry> m_orders;

    template <class Cmp>
    using Levels = std::map<Price, std::list<std::shared_ptr<Order>>, Cmp>;

    using Bids = Levels<std::greater<Price>>;
    using Asks = Levels<std::less<Price>>;

    Bids m_bids;
    Asks m_asks;
};
