#include "order_book.h"

vector<Trade> OrderBook::add_order(OrderId id, Side side, Price price, Quantity qty,
                        Quantity disp, Quantity display_size, OrderType type,
                        uint32_t ownerID, Price stop_price, uint32_t session_id)
{

    if (session_id != 0)
    {
        // Session validation for production use
    }
    stats_.totalOrders++;
    vector<Trade> trades;
    trades.reserve(16);

    Order *order_ptr = order_pool_.acquire();
    if (order_ptr == nullptr)
    {
        return {};
    }
    *order_ptr = Order(id, side, price, stop_price, qty, disp, display_size, type, ownerID, session_id);

    if (type != OrderType::MARKET && price > 0)
    {
        Price rounded_price = tick_table_.round_to_tick(price);
        if (rounded_price > 0)
        {
            order_ptr->price = rounded_price;
        }
    }
    if (stop_price > 0)
    {
        Price rounded_stop = tick_table_.round_to_tick(stop_price);
        if (rounded_stop > 0)
        {
            order_ptr->stop_price = rounded_stop;
        }
    }

    auto risk_result = risk_manager_.check_order(*order_ptr);
    if (risk_result != RiskManager::RiskResult::APPROVED)
    {
        order_pool_.release(order_ptr);
        stats_.totalRiskRejected++;
        return {};
    }

    if (type == OrderType::STOP_LOSS)
    {
        stop_manager_.add_stop_order(order_ptr);
        orders_[id] = order_ptr;
        return {};
    }
    else if (type == OrderType::FOK)
    {
        bool filled = add_FOK_order(order_ptr, trades);
        if (filled)
        {
            stats_.totalTrades += trades.size();
            for (auto &trade : trades)
            {
                stats_.totalVolumes += trade.quantity;
            }
        }
        order_pool_.release(order_ptr);
        process_triggered_stops(trades);
        return trades;
    }

    if (type == OrderType::MARKET)
    {
        add_market_order(order_ptr, trades);
        stats_.totalTrades += trades.size();
        for (auto &trade : trades)
        {
            stats_.totalVolumes += trade.quantity;
        }
        order_pool_.release(order_ptr);
        process_triggered_stops(trades);
        return trades;
    }

    if (side == Side::BUY)
    {
        auto &opposite = asks_;
        while (order_ptr->display > 0 && !opposite.empty())
        {
            auto &[best_price, level] = *opposite.begin();
            if (price < best_price)
                break;

            while (order_ptr->display > 0 && !level.empty())
            {
                Order *passive = level.front();
                if (order_ptr->ownerID == passive->ownerID)
                {
                    level.pop_front();
                    cancel_order(passive->id);
                    continue;
                }

                Quantity match_qty = min(order_ptr->display, passive->display);
                trades.push_back(execute_trade(order_ptr, passive, match_qty));

                order_ptr->display -= match_qty;
                passive->display -= match_qty;
                passive->remaining -= match_qty;

                if (passive->display == 0)
                {
                    level.pop_front();
                    bool order_refilled = handle_iceberg_refill(passive, level);
                    if (!order_refilled)
                    {
                        orders_.erase(passive->id);
                        order_pool_.release(passive);
                    }
                }
            }
            cleanup_empty_level(opposite, best_price);
        }
    }
    else
    {
        auto &opposite = bids_;
        while (order_ptr->display > 0 && !opposite.empty())
        {
            auto &[best_price, level] = *opposite.begin();
            if (price > best_price)
                break;

            while (order_ptr->display > 0 && !level.empty())
            {
                Order *passive = level.front();
                if (order_ptr->ownerID == passive->ownerID)
                {
                    level.pop_front();
                    cancel_order(passive->id);
                    continue;
                }

                Quantity match_qty = min(order_ptr->display, passive->display);
                trades.push_back(execute_trade(order_ptr, passive, match_qty));

                order_ptr->display -= match_qty;
                passive->display -= match_qty;
                passive->remaining -= match_qty;

                if (passive->display == 0)
                {
                    level.pop_front();
                    bool order_refilled = handle_iceberg_refill(passive, level);
                    if (!order_refilled)
                    {
                        orders_.erase(passive->id);
                        order_pool_.release(passive);
                    }
                }
            }
            cleanup_empty_level(opposite, best_price);
        }
    }

    // Add remaining to book if GTC or ICEBERG
    if (order_ptr->display > 0 && (type == OrderType::GTC || type == OrderType::ICEBERG))
    {
        if (side == Side::BUY)
        {
            bids_[price].add_order(order_ptr);
        }
        else
        {
            asks_[price].add_order(order_ptr);
        }
        orders_[id] = order_ptr;
    }
    else
    {
        order_pool_.release(order_ptr);
    }

    process_triggered_stops(trades);

    stats_.totalTrades += trades.size();
    for (auto &trade : trades)
    {
        stats_.totalVolumes += trade.quantity;
    }
    return trades;
}

void OrderBook::process_triggered_stops(vector<Trade> &trades)
{
    if (trades.empty())
        return;

    Price last_trade_price = trades.back().price;

    if (stop_cascade_depth_ >= MAX_CASCADE_DEPTH)
    {
        return;
    }

    auto triggered_stops = stop_manager_.check_triggered_stops(last_trade_price);

    for (Order *stop_order : triggered_stops)
    {
        if (processing_stops_.count(stop_order->id))
        {
            continue;
        }

        processing_stops_.insert(stop_order->id);
        stop_cascade_depth_++;
        stats_.totalStopTriggered++;

        stop_order->type = OrderType::MARKET;
        stop_order->price = 0;
        stop_order->is_triggered = true;

        vector<Trade> stop_trades;
        add_market_order(stop_order, stop_trades);
        trades.insert(trades.end(), stop_trades.begin(), stop_trades.end());

        orders_.erase(stop_order->id);
        order_pool_.release(stop_order);

        processing_stops_.erase(stop_order->id);
        stop_cascade_depth_--;
    }
}

void OrderBook::add_market_order(Order *order, vector<Trade> &trades)
{
    if (order->side == Side::BUY)
    {
        auto &opposite = asks_;
        while (order->display > 0 && !opposite.empty())
        {
            auto &[best_price, level] = *opposite.begin();

            while (order->display > 0 && !level.empty())
            {
                Order *passive = level.front();
                if (order->ownerID == passive->ownerID)
                {
                    level.pop_front();
                    orders_.erase(passive->id);
                    order_pool_.release(passive);
                    continue;
                }

                Quantity cur_quant = min(passive->display, order->display);
                trades.push_back(execute_trade(order, passive, cur_quant));

                passive->display -= cur_quant;
                passive->remaining -= cur_quant;
                order->display -= cur_quant;

                if (passive->display == 0)
                {
                    level.pop_front();
                    bool order_refilled = handle_iceberg_refill(passive, level);
                    if (!order_refilled)
                    {
                        orders_.erase(passive->id);
                        order_pool_.release(passive);
                    }
                }
            }
            cleanup_empty_level(opposite, best_price);
        }
    }
    else
    {
        auto &opposite = bids_;
        while (order->display > 0 && !opposite.empty())
        {
            auto &[best_price, level] = *opposite.begin();

            while (order->display > 0 && !level.empty())
            {
                Order *passive = level.front();
                if (order->ownerID == passive->ownerID)
                {
                    level.pop_front();
                    orders_.erase(passive->id);
                    order_pool_.release(passive);
                    continue;
                }

                Quantity cur_quant = min(passive->display, order->display);
                trades.push_back(execute_trade(order, passive, cur_quant));

                passive->display -= cur_quant;
                passive->remaining -= cur_quant;
                order->display -= cur_quant;

                if (passive->display == 0)
                {
                    level.pop_front();
                    bool order_refilled = handle_iceberg_refill(passive, level);
                    if (!order_refilled)
                    {
                        orders_.erase(passive->id);
                        order_pool_.release(passive);
                    }
                }
            }
            cleanup_empty_level(opposite, best_price);
        }
    }
}

bool OrderBook::add_FOK_order(Order *order, vector<Trade> &trades)
{
    Quantity needed = order->quantity;
    vector<pair<Order *, Quantity>> candidates;

    auto check_orders = [&](const deque<Order *> orders)
    {
        for (Order *passive : orders)
        {
            if (order->ownerID == passive->ownerID)
                continue;

            Quantity available = min(needed, passive->display);
            candidates.emplace_back(passive, available);
            needed -= available;

            if (needed <= 0)
                return true;
        }
        return false;
    };

    // Check if we can fill completely
    if (order->side == Side::BUY)
    {
        for (auto &[price_level, level] : asks_)
        {
            if (price_level > order->price)
                break;

            if (!check_orders(level.mm_orders) && needed > 0)
            {
                check_orders(level.regular_orders);
            }
            if (needed <= 0)
                break;
        }
    }
    else
    {
        for (auto &[price_level, level] : bids_)
        {
            if (price_level < order->price)
                break;

            if (!check_orders(level.mm_orders) && needed > 0)
            {
                check_orders(level.regular_orders);
            }
            if (needed <= 0)
                break;
        }
    }

    if (needed > 0)
        return false; // Cannot fill completely

    // Execute all trades
    for (auto &[passive, qty] : candidates)
    {
        trades.push_back(execute_trade(order, passive, qty));

        passive->display -= qty;
        passive->remaining -= qty;
        order->display -= qty;
        order->remaining -= qty;

        if (passive->display == 0)
        {
            auto &level = (passive->side == Side::BUY) ? bids_[passive->price] : asks_[passive->price];
            level.erase(passive);

            bool order_refilled = handle_iceberg_refill(passive, level);
            if (!order_refilled)
            {
                orders_.erase(passive->id);
                order_pool_.release(passive);
            }

            if (level.empty())
            {
                if (passive->side == Side::BUY)
                {
                    bids_.erase(passive->price);
                }
                else
                {
                    asks_.erase(passive->price);
                }
            }
        }
    }
    return true;
}

bool OrderBook::cancel_order(OrderId id)
{
    auto it = orders_.find(id);
    if (it == orders_.end())
        return false;

    Order *order = it->second;

    if (order->type == OrderType::STOP_LOSS)
    {
        stop_manager_.remove_stop_order(id);
    }
    else
    {

        if (order->side == Side::BUY)
        {
            auto price_it = bids_.find(order->price);
            if (price_it != bids_.end())
            {
                auto &level = price_it->second;
                level.erase(order);
                if (level.empty())
                {
                    bids_.erase(price_it);
                }
            }
        }
        else
        {
            auto price_it = asks_.find(order->price);
            if (price_it != asks_.end())
            {
                auto &level = price_it->second;
                level.erase(order);
                if (level.empty())
                {
                    asks_.erase(price_it);
                }
            }
        }
    }

    orders_.erase(it);
    order_pool_.release(order);
    stats_.totalCancelledOrders++;
    return true;
}