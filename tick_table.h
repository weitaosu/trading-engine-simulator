#pragma once
#include "types.h"

class TickSizeTable
{
private:
    struct TickRule
    {
        Price min_price;
        Price max_price;
        Price tick_size;

        TickRule(Price min_p, Price max_p, Price tick)
            : min_price(min_p), max_price(max_p), tick_size(tick) {}
    };

    vector<TickRule> rules_;

public:
    TickSizeTable()
    {
        add_rule(1, 99, 1);
        add_rule(100, 999, 1);
        add_rule(1000, 4999, 1);
        add_rule(5000, 9999, 1);
        add_rule(10000, 99999, 1);
        add_rule(100000, 499999, 5);
        add_rule(500000, 999999, 10);
        add_rule(1000000, LLONG_MAX, 100);
    }

    void add_rule(Price min_price, Price max_price, Price tick_size)
    {
        if (min_price > max_price || tick_size <= 0 || min_price < 0)
        {
            throw std::invalid_argument("Invalid tick rule parameters");
        }

        for (const auto &rule : rules_)
        {
            if (!(max_price < rule.min_price || min_price > rule.max_price))
            {
                throw std::invalid_argument("Overlapping tick rule ranges not allowed");
            }
        }

        rules_.emplace_back(min_price, max_price, tick_size);
        sort(rules_.begin(), rules_.end(),
             [](const TickRule &a, const TickRule &b)
             {
                 return a.min_price < b.min_price;
             });
    }

    Price round_to_tick(Price price) const
    {
        if (price <= 0)
            return 0;

        for (const auto &rule : rules_)
        {
            if (rule.min_price <= price && rule.max_price >= price)
            {
                Price half_tick = rule.tick_size / 2;
                return ((price + half_tick) / rule.tick_size) * rule.tick_size;
            }
        }

        return 0;
    }

    bool is_valid_price(Price price) const
    {
        return price == round_to_tick(price);
    }

    Price get_tick_size(Price price) const
    {
        if (price <= 0)
            return 0;

        for (const auto &rule : rules_)
        {
            if (rule.min_price <= price && rule.max_price >= price)
            {
                return rule.tick_size;
            }
        }

        return 0;
    }

    Price get_next_tick_up(Price price) const
    {
        Price tick = get_tick_size(price);
        if (tick == 0)
            return 0;

        Price rounded_price = round_to_tick(price);
        if (rounded_price == 0)
            return 0;

        Price next_price = rounded_price + tick;
        return round_to_tick(next_price);
    }

    Price get_next_tick_down(Price price) const
    {
        Price tick = get_tick_size(price);
        if (tick == 0)
            return 0;

        Price rounded_price = round_to_tick(price);
        if (rounded_price == 0)
            return 0;

        Price next_price = rounded_price - tick;
        return next_price > 0 ? round_to_tick(next_price) : 0;
    }

    void print_rules() const
    {
        cout << "\n╔══════════════════════════════════════╗" << endl;
        cout << "║         TICK SIZE RULES              ║" << endl;
        cout << "╚══════════════════════════════════════╝" << endl;
        cout << "Based on NYSE/NASDAQ Regulation NMS Rule 612\n"
             << endl;

        for (size_t i = 0; i < rules_.size(); ++i)
        {
            const auto &rule = rules_[i];
            cout << "  Rule " << (i + 1) << ": $"
                 << fixed << setprecision(2) << (rule.min_price / 100.0);

            if (rule.max_price == LLONG_MAX)
            {
                cout << "+ -> $" << (rule.tick_size / 100.0) << " tick" << endl;
            }
            else
            {
                cout << " - $" << (rule.max_price / 100.0)
                     << " -> $" << (rule.tick_size / 100.0) << " tick" << endl;
            }
        }
        cout << endl;
    }
};