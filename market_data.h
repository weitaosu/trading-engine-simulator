#pragma once
#include "types.h"
#include "tick_table.h"

class MarketDataGenerator
{
private:
    struct MarketState
    {
        Price last_price;
        Price bid_price;
        Price ask_price;
        double volatility;
        double momentum;
        size_t time_of_day; // 0-390 minutes from market open
        bool is_high_volume_period;
    };

    struct TraderProfile
    {
        enum Type
        {
            MARKET_MAKER,
            INSTITUTIONAL,
            RETAIL,
            HFT
        };
        Type type;
        double aggressiveness; // 0-1, higher = more market orders
        int min_size, max_size;
        double iceberg_probability;
        double stop_loss_probability;
    };

    mt19937 gen_;
    MarketState market_;
    vector<TraderProfile> trader_profiles_;
    TickSizeTable tick_table_;

    Price round_to_valid_tick(Price price) const
    {
        return tick_table_.round_to_tick(price);
    }

public:
    MarketDataGenerator(int seed = 42) : gen_(seed)
    {
        // Initialize market state - use $1000 range like unit tests
        market_.last_price = 100000;                     // $1000.00
        market_.bid_price = round_to_valid_tick(99999);  // $999.99
        market_.ask_price = round_to_valid_tick(100001); // $1000.01
        market_.volatility = 0.02;                       // 2% daily volatility
        market_.momentum = 0.0;
        market_.time_of_day = 0;
        market_.is_high_volume_period = true;

        setup_trader_profiles();
    }

    void setup_trader_profiles()
    {
        trader_profiles_.resize(100);

        // Market Makers (10% of traders, provide liquidity)
        for (int i = 0; i < 10; ++i)
        {
            trader_profiles_[i] = {
                TraderProfile::MARKET_MAKER,
                0.4, // Higher aggressiveness for more crossing
                100, 500,
                0.3, // 30% iceberg probability
                0.05 // 5% stop loss probability
            };
        }

        // Institutional Traders (20% of traders, large orders)
        for (int i = 10; i < 30; ++i)
        {
            trader_profiles_[i] = {
                TraderProfile::INSTITUTIONAL,
                0.7, // Higher aggressiveness for more matching
                500, 2000,
                0.7, // 70% iceberg probability for large orders
                0.2  // 20% stop loss probability
            };
        }

        // HFT/Algorithmic (15% of traders, fast execution)
        for (int i = 30; i < 45; ++i)
        {
            trader_profiles_[i] = {
                TraderProfile::HFT,
                0.9, // Very high aggressiveness - quick execution
                50, 300,
                0.1, // 10% iceberg probability
                0.15 // 15% stop loss probability
            };
        }

        // Retail Traders (55% of traders, small orders)
        for (int i = 45; i < 100; ++i)
        {
            trader_profiles_[i] = {
                TraderProfile::RETAIL,
                0.8, // Higher aggressiveness for more matching
                10, 200,
                0.05, // 5% iceberg probability
                0.25  // 25% stop loss probability
            };
        }
    }

    void update_market_dynamics()
    {
        uniform_real_distribution<> volatility_shock(-0.001, 0.001);
        uniform_real_distribution<> momentum_decay(-0.1, 0.1);

        // Update volatility with clustering effect
        market_.volatility += volatility_shock(gen_);
        market_.volatility = max(0.005, min(0.05, market_.volatility)); // Clamp between 0.5% and 5%

        // Update momentum with mean reversion
        market_.momentum += momentum_decay(gen_);
        market_.momentum *= 0.95; // Decay factor

        // Time-based volume patterns
        market_.time_of_day = (market_.time_of_day + 1) % 390; // 6.5 hour trading day
        market_.is_high_volume_period =
            (market_.time_of_day < 30) ||                              // Market open
            (market_.time_of_day > 360) ||                             // Market close
            (market_.time_of_day >= 90 && market_.time_of_day <= 120); // Lunch time spike

        // Price discovery with bounded random walk + mean reversion
        normal_distribution<> price_change(market_.momentum * 0.1, market_.volatility * 0.01);
        double change = price_change(gen_);

        Price base_increment = 50; // $0.50 base move
        Price price_increment = static_cast<Price>(change * base_increment * market_.volatility * 100);

        // Add mean reversion toward $1000 (100000 cents)
        Price target_price = 100000;
        Price mean_reversion = static_cast<Price>((target_price - market_.last_price) * 0.001); // 0.1% mean reversion

        Price new_price = market_.last_price + price_increment + mean_reversion;

        // Enforce price bounds to prevent collapse: $500 - $1500 range
        new_price = max(static_cast<Price>(50000), min(static_cast<Price>(150000), new_price));
        new_price = round_to_valid_tick(new_price);

        if (new_price > 0)
        {
            market_.last_price = new_price;

            Price tick_size = tick_table_.get_tick_size(market_.last_price);
            Price min_spread = tick_size;                                                                 // Minimum 1-tick spread
            Price volatility_spread = static_cast<Price>(market_.volatility * market_.last_price * 0.05); // Reduced from 0.1
            Price spread = round_to_valid_tick(max(min_spread, volatility_spread));

            market_.bid_price = round_to_valid_tick(market_.last_price - spread / 2);
            market_.ask_price = round_to_valid_tick(market_.last_price + spread / 2);

            // Ensure spread is at least 1 tick
            if (market_.ask_price - market_.bid_price < tick_size)
            {
                market_.ask_price = market_.bid_price + tick_size;
            }
        }
    }

    void generate_realistic_order(ofstream &file, size_t order_id, size_t total_count);

    void print_market_state() const
    {
        cout << "\n Current Market State:" << endl;
        cout << "  Last Price: $" << fixed << setprecision(2) << (market_.last_price / 100.0) << endl;
        cout << "  Bid: $" << (market_.bid_price / 100.0) << " | Ask: $" << (market_.ask_price / 100.0) << endl;
        cout << "  Spread: $" << ((market_.ask_price - market_.bid_price) / 100.0) << " ("
             << fixed << setprecision(1) << (100.0 * (market_.ask_price - market_.bid_price) / market_.last_price) << " bps)" << endl;
        cout << "  Volatility: " << (market_.volatility * 100.0) << "% | Momentum: "
             << (market_.momentum > 0 ? "+" : "") << (market_.momentum * 100.0) << "%" << endl;
        cout << "  Time: " << (market_.time_of_day / 60) << "h:"
             << setfill('0') << setw(2) << (market_.time_of_day % 60) << "m"
             << (market_.is_high_volume_period ? " (High Volume)" : " (Normal Volume)") << endl;
    }
};