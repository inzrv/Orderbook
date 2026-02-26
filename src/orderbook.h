#pragma once

#include "types/order.h"
#include "types/change.h"
#include "types/trade.h"

#include <map>
#include <memory>
#include <list>
#include <thread>
#include <condition_variable>

class Orderbook
{
public:
    struct AggregatedLevel
    {
        Quantity quantity{0};
        uint32_t count{0};
    };

    enum class Action
    {
        UNKNOWN,
        ADD,
        REMOVE,
        MATCH
    };

    struct OrderEntry
    {
        std::shared_ptr<Order> order{nullptr};
        std::list<std::shared_ptr<Order>>::iterator location{};
    };

    Orderbook();
    ~Orderbook();
    std::vector<Trade> add(std::shared_ptr<Order> order);
    void cancel(Order::Id order_id);
    std::vector<Trade> modify(Order::Id order_id, const Change& change);

private:
    std::vector<Trade> match();
    Trade matchTop();
    bool canMatch(Side side, Price price) const;
    std::shared_ptr<Order> processMAR(std::shared_ptr<Order> order) const;
    std::optional<Price> bestPrice(Side side) const;
    void pruneGFD();
    void cancel(const std::vector<Order::Id>& order_ids);
    void cancelImpl(Order::Id order_id);
    void cancelFAKs();
    std::vector<Trade> addImpl(std::shared_ptr<Order> order);
    std::chrono::system_clock::time_point nextPruneTime() const;

    void onCancel(std::shared_ptr<Order> order);
    void onAdd(std::shared_ptr<Order> order);
    void onMatch(Side side, Price price, Quantity quantity, bool is_fully_filled);

    void updateAggregatedLevel(Side side, Price price, Quantity quantity, Action action);
    bool canFullyFill(Side side, Price price, Quantity quantity) const;
    bool canFullyFillBid(Price price, Quantity quantity) const;
    bool canFullyFillAsk(Price price, Quantity quantity) const;

private:
    std::unordered_map<Order::Id, OrderEntry> m_orders;

    template <class Cmp>
    using Levels = std::map<Price, std::list<std::shared_ptr<Order>>, Cmp>;

    using Bids = Levels<std::greater<Price>>;
    using Asks = Levels<std::less<Price>>;

    Bids m_bids;
    Asks m_asks;

    template <class Cmp>
    using AggregatedLevels = std::map<Price, AggregatedLevel, Cmp>;
    using AggregatedBids = AggregatedLevels<std::greater<Price>>;
    using AggregatedAsks = AggregatedLevels<std::less<Price>>;

    AggregatedBids m_aggregated_bids;
    AggregatedAsks m_aggregated_asks;

    static constexpr uint32_t kPruneHour = 16;

    mutable std::mutex m_orders_mutex;
    std::thread m_orders_prune_thread;
    std::condition_variable m_shutdown_cv;
    std::atomic<bool> m_shutdown{false};
};
