#pragma once
#include <iostream>
#include <map>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <iomanip>
#include <numeric>
#include <mutex>
#include <stack>

using namespace std;
using namespace std::chrono;

using OrderId = uint64_t;
using Price = int64_t;
using Quantity = int64_t;

enum class Side
{
    BUY,
    SELL
};

enum class OrderType
{
    GTC,
    IOC,
    FOK,
    MARKET,
    STOP_LOSS,
    ICEBERG,
};

struct Order
{
    OrderId id;
    Side side;
    Price price;
    Price stop_price;
    Quantity quantity;
    Quantity remaining;
    Quantity display;
    Quantity display_size;
    OrderType type;
    int64_t timestamp;
    uint32_t ownerID;
    bool is_triggered;
    OrderId parent_id;
    bool is_market_maker;
    uint32_t session_id;

    Order() : id(0), side(Side::BUY), price(0), stop_price(0), quantity(0),
              remaining(0), display(0), display_size(0), type(OrderType::GTC),
              timestamp(0), ownerID(0), is_triggered(false), parent_id(0) {}

    Order(OrderId id, Side side, Price price, Price stop_price, Quantity qty,
          Quantity disp, Quantity display_size, OrderType type, uint32_t ownerID, uint32_t sessionID = 0)
        : id(id), side(side), price(price), stop_price(stop_price), quantity(qty),
          remaining(qty), display(disp), display_size(display_size), type(type),
          ownerID(ownerID), is_triggered(false), parent_id(0), session_id(sessionID)
    {
        timestamp = duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count();
    }
};

struct Trade
{
    OrderId buy_id, sell_id;
    Price price;
    Quantity quantity;
    int64_t timestamp;

    Trade() : buy_id(0), sell_id(0), price(0), quantity(0), timestamp(0) {}

    Trade(OrderId bid, OrderId sid, Price p, Quantity q, int64_t ts)
        : buy_id(bid), sell_id(sid), price(p), quantity(q), timestamp(ts) {}
};