#include "market_data.h"

void MarketDataGenerator::generate_realistic_order(ofstream &file, size_t order_id, size_t total_count)
{
    uniform_int_distribution<> trader_dist(0, 99);
    uniform_real_distribution<> prob_dist(0.0, 1.0);
    uniform_real_distribution<> price_offset(-0.005, 0.005); // ±0.5% price variation

    uint32_t trader_id = trader_dist(gen_) + 1;
    const auto &profile = trader_profiles_[trader_id - 1];

    // Determine order type based on trader profile and market conditions
    // Build book first with limit orders, then add crossing orders
    string order_type;
    double type_rand = prob_dist(gen_);

    // For first 10% of orders, favor limit orders to build the book
    bool build_book_phase = (order_id <= total_count * 0.1);

    if (build_book_phase)
    {
        // Favor limit orders during book building - NO MARKET ORDERS
        if (type_rand < 0.8)
        {
            order_type = "GTC";
        }
        else
        {
            order_type = "ICEBERG";
        }
    }
    else
    {
        // Normal trading phase with more aggressive orders
        if (type_rand < profile.aggressiveness * (market_.is_high_volume_period ? 1.5 : 1.0))
        {
            order_type = "MARKET";
        }
        else if (type_rand < profile.aggressiveness + profile.iceberg_probability)
        {
            order_type = "ICEBERG";
        }
        else if (type_rand < profile.aggressiveness + profile.iceberg_probability + profile.stop_loss_probability)
        {
            order_type = "STOP_LOSS";
        }
        else if (type_rand < 0.95)
        {
            order_type = "GTC";
        }
        else
        {
            order_type = (prob_dist(gen_) < 0.5) ? "IOC" : "FOK";
        }
    }

    // Determine order size based on trader type
    uniform_int_distribution<> size_dist(profile.min_size, profile.max_size);
    Quantity quantity = size_dist(gen_);

    // Adjust quantity based on time of day and volatility
    if (market_.is_high_volume_period)
    {
        quantity = static_cast<Quantity>(quantity * (1.0 + prob_dist(gen_) * 0.5)); // Up to 50% larger
    }

    // Determine side with slight momentum bias
    bool is_buy;
    if (abs(market_.momentum) > 0.01)
    {
        is_buy = (market_.momentum > 0) ? (prob_dist(gen_) < 0.6) : (prob_dist(gen_) < 0.4);
    }
    else
    {
        is_buy = prob_dist(gen_) < 0.5;
    }

    // Determine price based on order type and trader profile
    Price order_price = 0;
    Price stop_price = 0;

    if (order_type == "MARKET")
    {
        order_price = 0; // Market orders have no price limit
    }
    else if (order_type == "STOP_LOSS")
    {
        // Stop orders: 2-5% away from current price
        uniform_real_distribution<> stop_dist(0.02, 0.05);
        double stop_offset = stop_dist(gen_);

        if (is_buy)
        {
            stop_price = round_to_valid_tick(static_cast<Price>(market_.last_price * (1.0 + stop_offset)));
            order_price = round_to_valid_tick(market_.ask_price);
        }
        else
        {
            stop_price = round_to_valid_tick(static_cast<Price>(market_.last_price * (1.0 - stop_offset)));
            order_price = round_to_valid_tick(market_.bid_price);
        }
    }
    else
    {
        // Limit orders: price based on trader aggressiveness and current spread
        double aggressiveness_factor = profile.aggressiveness;

        if (profile.type == TraderProfile::MARKET_MAKER)
        {
            // Market makers provide liquidity with tight spreads
            Price tick_size = tick_table_.get_tick_size(market_.last_price);
            if (is_buy)
            {
                // Market makers bid inside the spread or occasionally cross
                if (prob_dist(gen_) < 0.2) // Only 20% chance to cross spread
                {
                    order_price = market_.ask_price; // Cross the spread aggressively
                }
                else if (prob_dist(gen_) < 0.7) // 50% at current bid
                {
                    order_price = market_.bid_price; // At best bid
                }
                else // 30% improve the bid slightly
                {
                    order_price = market_.bid_price + tick_size; // Improve bid by 1 tick
                }
            }
            else
            {
                // Market makers offer inside the spread or occasionally cross
                if (prob_dist(gen_) < 0.2) // Only 20% chance to cross spread
                {
                    order_price = market_.bid_price; // Cross the spread aggressively
                }
                else if (prob_dist(gen_) < 0.7) // 50% at current ask
                {
                    order_price = market_.ask_price; // At best ask
                }
                else // 30% improve the ask slightly
                {
                    order_price = market_.ask_price - tick_size; // Improve ask by 1 tick
                }
            }
        }
        else
        {
            // Other traders: spread between inside market and aggressive
            if (is_buy)
            {
                // Buy orders: aggressive = market ask, passive = market bid
                Price aggressive_price = market_.ask_price;
                Price passive_price = market_.bid_price;
                order_price = passive_price + static_cast<Price>(aggressiveness_factor * (aggressive_price - passive_price));
                order_price = round_to_valid_tick(order_price);
            }
            else
            {
                // Sell orders: aggressive = market bid, passive = market ask
                Price aggressive_price = market_.bid_price;
                Price passive_price = market_.ask_price;
                order_price = passive_price - static_cast<Price>(aggressiveness_factor * (passive_price - aggressive_price));
                order_price = round_to_valid_tick(order_price);
            }
        }
    }

    // Ensure valid tick-compliant prices
    if (order_price > 0)
    {
        order_price = round_to_valid_tick(max(Price(1), order_price));
    }
    if (stop_price > 0)
    {
        stop_price = round_to_valid_tick(stop_price);
    }

    // Display size and iceberg logic
    Quantity display_size = quantity;
    Quantity disp = quantity;

    if (order_type == "ICEBERG")
    {
        uniform_int_distribution<> iceberg_dist(quantity / 10, quantity / 3);
        display_size = min(quantity, static_cast<Quantity>(iceberg_dist(gen_)));
        disp = display_size;
    }

    uint32_t session_id = (order_id % 500) + 1;

    string ip_address = "192.168." + to_string((order_id % 200) / 50) + "." + to_string((order_id % 50) + 1);

    file << order_id << "," << (is_buy ? "BUY" : "SELL") << ","
         << order_price << "," << quantity << ","
         << order_type << "," << disp << "," << display_size << "," << trader_id
         << "," << stop_price << "," << session_id << "," << ip_address << "\n";
}