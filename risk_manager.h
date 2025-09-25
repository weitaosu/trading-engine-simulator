#pragma once
#include "types.h"
#include "tick_table.h"

class CircuitBreaker
{
private:
    Price upper_limit_;
    Price lower_limit_;
    bool is_triggered_;
    time_t trigger_time_;

public:
    CircuitBreaker() : upper_limit_(0), lower_limit_(0), is_triggered_(false), trigger_time_(0) {}

    void set_limits(Price reference, double percentage)
    {
        upper_limit_ = reference * (1.0 + percentage);
        lower_limit_ = reference * (1.0 - percentage);
        is_triggered_ = false;
    }

    bool should_halt_trading(Price price)
    {
        if (price >= upper_limit_ || price <= lower_limit_)
        {
            if (!is_triggered_)
            {
                is_triggered_ = true;
                trigger_time_ = time(nullptr);
                return true;
            }
        }
        return false;
    }

    bool is_trading_halted() const { return is_triggered_; }

    void resume_trading() { is_triggered_ = false; }
};

class RiskManager
{
public:
    struct RiskLimits
    {
        int64_t max_position;
        int64_t max_order_value;
        int64_t max_order_qty;
        int64_t daily_loss_limit;
        double max_price_deviation;
        int32_t max_orders_per_sec;
        int64_t max_daily_volume;
    };

    struct Position
    {
        int64_t quantity;
        int64_t unrealized_pnl;
        int64_t realized_pnl;
        Price avg_price;
        int64_t daily_volume;
    };

    enum class RiskResult
    {
        APPROVED,
        REJECTED_POSITION_LIMIT,
        REJECTED_ORDER_SIZE,
        REJECTED_FAT_FINGER,
        REJECTED_LOSS_LIMIT,
        REJECTED_RATE_LIMIT,
        REJECTED_CIRCUIT_BREAKER,
        REJECTED_VOLUME_LIMIT,
        REJECTED_INVALID_TICK_SIZE
    };

private:
    struct RateLimit
    {
        deque<int64_t> timestamps;
        int32_t count_this_second;
    };

    unordered_map<uint32_t, Position> positions_;
    unordered_map<uint32_t, RiskLimits> trader_limits_;
    unordered_map<uint32_t, RateLimit> rate_limits_;
    Price last_trade_price_;
    CircuitBreaker circuit_breaker_;
    const TickSizeTable *tick_table_;

public:
    RiskManager() : last_trade_price_(0), tick_table_(nullptr) {}

    RiskResult check_order(const Order &order)
    {
        if (order.type == OrderType::STOP_LOSS)
        {
            return RiskResult::APPROVED;
        }

        auto &pos = positions_[order.ownerID];
        auto limit_it = trader_limits_.find(order.ownerID);
        if (limit_it == trader_limits_.end())
        {
            return RiskResult::REJECTED_POSITION_LIMIT;
        }
        auto &limit = limit_it->second;

        int64_t newpos = (order.side == Side::BUY) ? (pos.quantity + order.quantity) : (pos.quantity - order.quantity);
        if (abs(newpos) > limit.max_position)
        {
            return RiskResult::REJECTED_POSITION_LIMIT;
        }

        if (order.quantity > limit.max_order_qty)
        {
            return RiskResult::REJECTED_ORDER_SIZE;
        }

        if (order.price * order.quantity > limit.max_order_value)
        {
            return RiskResult::REJECTED_ORDER_SIZE;
        }

        if (last_trade_price_ > 0 && order.price > 0 &&
            (abs(order.price - last_trade_price_) / double(last_trade_price_)) > limit.max_price_deviation)
        {
            return RiskResult::REJECTED_FAT_FINGER;
        }

        if (pos.realized_pnl + pos.unrealized_pnl < -limit.daily_loss_limit)
        {
            return RiskResult::REJECTED_LOSS_LIMIT;
        }

        if (is_rate_limited(order.ownerID))
        {
            return RiskResult::REJECTED_RATE_LIMIT;
        }

        if (circuit_breaker_.should_halt_trading(order.price))
        {
            return RiskResult::REJECTED_CIRCUIT_BREAKER;
        }

        return RiskResult::APPROVED;
    }

    void update_position(uint32_t trader_id, const Trade &trade, Side trader_side)
    {
        auto &pos = positions_[trader_id];

        if (trader_side == Side::BUY)
        {
            if (pos.quantity == 0)
            {
                pos.avg_price = trade.price;
            }
            else if (pos.quantity > 0)
            {
                pos.avg_price = (pos.quantity * pos.avg_price + trade.price * trade.quantity) / (pos.quantity + trade.quantity);
            }
            else
            {
                int64_t shares_to_cover = min(-pos.quantity, trade.quantity);
                pos.realized_pnl += (pos.avg_price - trade.price) * shares_to_cover;
                if (trade.quantity > -pos.quantity)
                {
                    pos.avg_price = trade.price;
                }
            }
            pos.quantity += trade.quantity;
        }
        else
        {
            if (pos.quantity == 0)
            {
                pos.avg_price = trade.price;
            }
            else if (pos.quantity < 0)
            {
                pos.avg_price = (-pos.quantity * pos.avg_price + trade.quantity * trade.price) / (-pos.quantity + trade.quantity);
            }
            else
            {
                int64_t shares_to_cover = min(pos.quantity, trade.quantity);
                pos.realized_pnl += (trade.price - pos.avg_price) * shares_to_cover;
                if (trade.quantity > pos.quantity)
                {
                    pos.avg_price = trade.price;
                }
            }
            pos.quantity -= trade.quantity;
        }

        pos.daily_volume += trade.quantity;
        last_trade_price_ = trade.price;
    }

    void set_trader_limits(uint32_t trader_id, const RiskLimits &limits)
    {
        if (limits.max_position <= 0 || limits.max_order_qty <= 0 || limits.max_order_value <= 0 ||
            limits.daily_loss_limit <= 0 || limits.max_price_deviation <= 0 || limits.max_price_deviation > 1.0 ||
            limits.max_orders_per_sec <= 0)
        {
            throw invalid_argument("Invalid risk limits provided");
        }

        trader_limits_[trader_id] = limits;

        if (positions_.find(trader_id) == positions_.end())
        {
            positions_[trader_id] = Position{0, 0, 0, 0, 0};
        }

        if (rate_limits_.find(trader_id) == rate_limits_.end())
        {
            rate_limits_[trader_id] = RateLimit{};
        }
    }

    bool is_rate_limited(uint32_t trader_id)
    {
        auto it = trader_limits_.find(trader_id);
        if (it == trader_limits_.end())
        {
            return true;
        }

        auto &limit = it->second;
        auto &rate_limit = rate_limits_[trader_id];
        auto now = duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count();

        while (!rate_limit.timestamps.empty() && (now - rate_limit.timestamps.front()) > 1000000000LL)
        {
            rate_limit.timestamps.pop_front();
        }

        if (static_cast<int32_t>(rate_limit.timestamps.size()) >= limit.max_orders_per_sec)
        {
            return true;
        }

        rate_limit.timestamps.push_back(now);
        return false;
    }

    void reset_daily_stats()
    {
        for (auto &[trader_id, pos] : positions_)
        {
            pos.daily_volume = 0;
            pos.realized_pnl = 0;
            pos.unrealized_pnl = 0;
        }

        for (auto &[id, limit] : rate_limits_)
        {
            limit.count_this_second = 0;
            limit.timestamps.clear();
        }

        last_trade_price_ = 0;
        circuit_breaker_.resume_trading();
    }

    Position get_position(uint32_t trader_id)
    {
        auto it = positions_.find(trader_id);
        if (it == positions_.end())
        {
            return Position{0, 0, 0, 0, 0};
        }

        auto pos = it->second;
        if (last_trade_price_ > 0 && pos.quantity != 0)
        {
            pos.unrealized_pnl = (last_trade_price_ - pos.avg_price) * pos.quantity;
        }
        return pos;
    }

    void mark_to_market(Price current_price)
    {
        if (current_price <= 0)
            return;

        for (auto &[trader_id, pos] : positions_)
        {
            if (pos.quantity != 0)
            {
                pos.unrealized_pnl = (current_price - pos.avg_price) * pos.quantity;

                auto limit_it = trader_limits_.find(trader_id);
                if (limit_it != trader_limits_.end())
                {
                    int64_t total_loss = pos.unrealized_pnl + pos.realized_pnl;
                    if (total_loss < -static_cast<int64_t>(limit_it->second.daily_loss_limit * 0.9))
                    {
                    }
                }
            }
        }

        last_trade_price_ = current_price;
        if (circuit_breaker_.should_halt_trading(current_price))
        {
        }
    }

    CircuitBreaker &get_circuit_breaker() { return circuit_breaker_; }
    Price get_last_trade_price() const { return last_trade_price_; }
    void set_tick_table(const TickSizeTable *tick_table) { tick_table_ = tick_table; }
};