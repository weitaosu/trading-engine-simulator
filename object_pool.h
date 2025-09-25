#pragma once
#include "types.h"

template <typename T>
class ObjectPool
{
private:
    vector<unique_ptr<T>> pool_;
    stack<T *> available_;
    size_t pool_size_;
    size_t allocated_count_;
    unordered_set<T *> valid_objects_;
    mutable std::mutex pool_mutex_;

public:
    ObjectPool(size_t initial_size = 10000) : pool_size_(initial_size), allocated_count_(0)
    {
        pool_.reserve(pool_size_);
        for (size_t i = 0; i < pool_size_; i++)
        {
            auto obj = std::make_unique<T>();
            available_.push(obj.get());
            valid_objects_.insert(obj.get());
            pool_.push_back(std::move(obj));
        }
    }

    T *acquire()
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);

        if (available_.empty())
        {
            auto obj = std::make_unique<T>();
            T *ptr = obj.get();
            valid_objects_.insert(ptr);
            pool_.push_back(std::move(obj));
            allocated_count_++;

            return ptr;
        }
        else
        {
            T *ptr = available_.top();
            available_.pop();
            allocated_count_++;

            return ptr;
        }
    }

    void release(T *obj)
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        if (obj == nullptr || valid_objects_.find(obj) == valid_objects_.end())
            return;

        if constexpr (std::is_same_v<T, Order>)
        {
            reset_order(static_cast<Order *>(obj));
        }
        else if constexpr (std::is_same_v<T, Trade>)
        {
            reset_trade(static_cast<Trade *>(obj));
        }
        available_.push(obj);
        allocated_count_--;
    }

    size_t available_count() const
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        return available_.size();
    }

    size_t allocated_count() const
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        return allocated_count_;
    }

    size_t total_capacity() const
    {
        return pool_.size();
    }

    void expand_pool(size_t additional_size = 1000)
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        size_t prev_size = pool_size_;
        pool_size_ += additional_size;
        for (size_t i = prev_size; i < pool_size_; i++)
        {
            auto obj = std::make_unique<T>();
            available_.push(obj.get());
            valid_objects_.insert(obj.get());
            pool_.push_back(std::move(obj));
        }
    }

private:
    void reset_order(Order *order)
    {
        order->id = 0;
        order->price = 0;
        order->stop_price = 0;
        order->quantity = 0;
        order->remaining = 0;
        order->display = 0;
        order->display_size = 0;
        order->timestamp = 0;
        order->ownerID = 0;
        order->is_triggered = false;
        order->parent_id = 0;
        order->session_id = 0;
    }

    void reset_trade(Trade *trade)
    {
        trade->buy_id = 0;
        trade->price = 0;
        trade->quantity = 0;
        trade->sell_id = 0;
        trade->timestamp = 0;
    }
};