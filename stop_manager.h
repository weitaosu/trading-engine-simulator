#pragma once
#include "types.h"

class StopOrderManager
{
private:
    map<Price, vector<Order *>> buy_stops_;
    map<Price, vector<Order *>> sell_stops_;
    unordered_map<OrderId, Order *> stop_lookup_;

public:
    void add_stop_order(Order *order)
    {
        if (order->type != OrderType::STOP_LOSS)
            return;

        if (order->side == Side::BUY)
        {
            buy_stops_[order->stop_price].push_back(order);
        }
        else
        {
            sell_stops_[order->stop_price].push_back(order);
        }
        stop_lookup_[order->id] = order;
    }

    bool remove_stop_order(OrderId order_id)
    {
        auto it = stop_lookup_.find(order_id);
        if (it == stop_lookup_.end())
            return false;

        auto order = it->second;
        auto &bucket = (order->side == Side::BUY) ? buy_stops_[order->stop_price] : sell_stops_[order->stop_price];

        auto order_it = find(bucket.begin(), bucket.end(), order);
        if (order_it != bucket.end())
        {
            bucket.erase(order_it);
        }

        if (bucket.empty())
        {
            if (order->side == Side::BUY)
            {
                buy_stops_.erase(order->stop_price);
            }
            else
            {
                sell_stops_.erase(order->stop_price);
            }
        }

        stop_lookup_.erase(it);
        return true;
    }

    vector<Order *> check_triggered_stops(Price last_trade_price)
    {
        vector<Order *> triggered_orders;

        auto buy_end = buy_stops_.upper_bound(last_trade_price);
        for (auto it = buy_stops_.begin(); it != buy_end; ++it)
        {
            for (auto order : it->second)
            {
                triggered_orders.push_back(order);
                stop_lookup_.erase(order->id);
            }
        }
        buy_stops_.erase(buy_stops_.begin(), buy_end);

        auto sell_start = sell_stops_.lower_bound(last_trade_price);
        for (auto it = sell_start; it != sell_stops_.end(); ++it)
        {
            for (auto order : it->second)
            {
                triggered_orders.push_back(order);
                stop_lookup_.erase(order->id);
            }
        }
        sell_stops_.erase(sell_start, sell_stops_.end());

        return triggered_orders;
    }

    size_t pending_stop_count() const
    {
        return stop_lookup_.size();
    }
};