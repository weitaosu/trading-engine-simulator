#pragma once
#include "types.h"
#include "object_pool.h"
#include "risk_manager.h"
#include "stop_manager.h"
#include "tick_table.h"
#include "session_management.h"

struct PriceLevel
{
    deque<Order *> mm_orders;
    deque<Order *> regular_orders;

    Order *front()
    {
        if (!mm_orders.empty())
        {
            return mm_orders.front();
        }
        else
        {
            return regular_orders.front();
        }
        return nullptr;
    }

    void pop_front()
    {
        if (!mm_orders.empty())
        {
            mm_orders.pop_front();
        }
        else
        {
            regular_orders.pop_front();
        }
    }

    void add_order(Order *order)
    {
        if (order->is_market_maker)
        {
            mm_orders.push_back(order);
        }
        else
        {
            regular_orders.push_back(order);
        }
    }

    bool empty()
    {
        return mm_orders.empty() && regular_orders.empty();
    }

    void erase(Order *order)
    {
        auto it = find(mm_orders.begin(), mm_orders.end(), order);
        if (it != mm_orders.end())
        {
            mm_orders.erase(it);
        }
        else
        {
            it = find(regular_orders.begin(), regular_orders.end(), order);
            if (it != regular_orders.end())
            {
                regular_orders.erase(it);
            }
        }
    }
};

class OrderBook
{
private:
    map<Price, PriceLevel, greater<Price>> bids_;
    map<Price, PriceLevel, less<Price>> asks_;
    unordered_map<OrderId, Order *> orders_;
    ObjectPool<Order> order_pool_;
    ObjectPool<Trade> trade_pool_;
    vector<Trade *> trade_buffer_;

    RiskManager risk_manager_;
    StopOrderManager stop_manager_;
    TickSizeTable tick_table_;
    SessionManager session_manager_;

    unordered_set<OrderId> processing_stops_;
    int stop_cascade_depth_;
    static constexpr int MAX_CASCADE_DEPTH = 3;

    Trade execute_trade(Order *aggressive, Order *passive, Quantity qty)
    {
        Trade trade{
            aggressive->side == Side::BUY ? aggressive->id : passive->id,
            aggressive->side == Side::SELL ? aggressive->id : passive->id,
            passive->price,
            qty,
            duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count()};

        uint32_t buyer_id = (aggressive->side == Side::BUY) ? aggressive->ownerID : passive->ownerID;
        uint32_t seller_id = (aggressive->side == Side::SELL) ? aggressive->ownerID : passive->ownerID;

        risk_manager_.update_position(buyer_id, trade, Side::BUY);
        risk_manager_.update_position(seller_id, trade, Side::SELL);

        return trade;
    }

    struct Stats
    {
        uint64_t totalOrders = 0;
        uint64_t totalTrades = 0;
        uint64_t totalVolumes = 0;
        uint64_t totalCancelledOrders = 0;
        uint64_t totalIOC_rejected = 0;
        uint64_t totalStopTriggered = 0;
        uint64_t totalRiskRejected = 0;
    } stats_;

    bool handle_iceberg_refill(Order *order, PriceLevel &level)
    {
        if (order->type == OrderType::ICEBERG && order->remaining > 0)
        {
            order->display = min(order->remaining, order->display_size);
            order->remaining -= order->display;
            level.add_order(order);
            return true;
        }
        return false;
    }

    template <typename BookType>
    void cleanup_empty_level(BookType &book, Price price)
    {
        auto price_it = book.find(price);
        if (price_it != book.end() && price_it->second.empty())
        {
            book.erase(price_it);
        }
    }

public:
    OrderBook() : order_pool_(2000000), trade_pool_(500000), stop_cascade_depth_(0)
    {
        orders_.reserve(500000);
        trade_buffer_.reserve(1000);

        risk_manager_.set_tick_table(&tick_table_);
    }

    size_t get_order_pool_available() const { return order_pool_.available_count(); }

    ~OrderBook()
    {
        for (auto &[id, order] : orders_)
        {
            order_pool_.release(order);
        }

        for (Trade *trade : trade_buffer_)
        {
            trade_pool_.release(trade);
        }
    }

    RiskManager &get_risk_manager() { return risk_manager_; }
    StopOrderManager &get_stop_manager() { return stop_manager_; }

    vector<Trade> add_order(OrderId id, Side side, Price price, Quantity qty,
                            Quantity disp, Quantity display_size, OrderType type,
                            uint32_t ownerID, Price stop_price = 0, uint32_t session_id = 0);

    void process_triggered_stops(vector<Trade> &trades);
    void add_market_order(Order *order, vector<Trade> &trades);
    bool add_FOK_order(Order *order, vector<Trade> &trades);
    bool cancel_order(OrderId id);

    size_t order_count() const { return orders_.size(); }
    size_t bid_levels() const { return bids_.size(); }
    size_t ask_levels() const { return asks_.size(); }
    Price best_bid() const { return bids_.empty() ? 0 : bids_.begin()->first; }
    Price best_ask() const { return asks_.empty() ? 0 : asks_.begin()->first; }

    void print_stats() const {}

    void print_pool_stats() const
    {
        cout << "\n=== MEMORY POOL STATISTICS ===" << endl;
        cout << "Order Pool - Available: " << order_pool_.available_count()
             << ", Allocated: " << order_pool_.allocated_count()
             << ", Capacity: " << order_pool_.total_capacity() << endl;
        cout << "Trade Pool - Available: " << trade_pool_.available_count()
             << ", Allocated: " << trade_pool_.allocated_count()
             << ", Capacity: " << trade_pool_.total_capacity() << endl;

        // Pool efficiency metrics
        double order_utilization = double(order_pool_.allocated_count()) / order_pool_.total_capacity() * 100.0;
        double trade_utilization = double(trade_pool_.allocated_count()) / trade_pool_.total_capacity() * 100.0;
        cout << "Order Pool Utilization: " << fixed << setprecision(1) << order_utilization << "%" << endl;
        cout << "Trade Pool Utilization: " << fixed << setprecision(1) << trade_utilization << "%" << endl;

        if (order_utilization > 80.0)
        {
            cout << "  WARNING: Order pool utilization >80% - consider expanding" << endl;
        }
        if (trade_utilization > 80.0)
        {
            cout << "  WARNING: Trade pool utilization >80% - consider expanding" << endl;
        }
    }
};