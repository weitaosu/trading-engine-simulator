#include "order_book.h"
#include "market_data.h"

void setup_demo_risk_limits(OrderBook &book)
{
    RiskManager::RiskLimits default_limits = {
        .max_position = 100000,       // 100K shares - realistic institutional limit
        .max_order_value = 50000000,  // $500K max order value - realistic institutional
        .max_order_qty = 10000,       // 10K shares max - realistic institutional order
        .daily_loss_limit = 1000000,  // $10K daily loss limit - realistic institutional
        .max_price_deviation = 0.10,  // 10% price deviation - realistic fat finger protection
        .max_orders_per_sec = 1000,   // 1000 orders/sec - realistic for algorithms
        .max_daily_volume = 1000000}; // 1M shares daily volume - realistic institutional

    auto &risk_mgr = book.get_risk_manager();
    for (uint32_t trader_id = 1; trader_id <= 100; ++trader_id)
    {
        risk_mgr.set_trader_limits(trader_id, default_limits);
    }

    risk_mgr.get_circuit_breaker().set_limits(100000, 0.20);

    // Initialize with a reasonable market price to avoid rejecting first orders
    risk_mgr.mark_to_market(100000);
}

void generate_test_data(const string &filename, size_t count)
{

    MarketDataGenerator generator;
    ofstream file(filename);
    file << "order_id,side,price,quantity,type,disp,display_size,owner,stop_price,session_id,ip_address\n";

    for (size_t i = 1; i <= count; ++i)
    {
        if (i % 50 == 0) // Update market every 50 orders
        {
            generator.update_market_dynamics();
        }

        generator.generate_realistic_order(file, i, count);
    }

    generator.print_market_state();
}

void run_benchmark(const string &filename)
{
    OrderBook book;
    setup_demo_risk_limits(book);

    ifstream file(filename);
    if (!file.is_open())
    {
        cerr << "Error: Cannot open file " << filename << endl;
        return;
    }

    string line;
    getline(file, line);

    vector<int64_t> latencies;
    latencies.reserve(1000000);
    size_t order_count = 0;
    size_t trade_count = 0;
    size_t rejected_count = 0;

    auto start_time = high_resolution_clock::now();

    while (getline(file, line))
    {
        auto order_start = high_resolution_clock::now();

        stringstream ss(line);
        string token;
        vector<string> tokens;
        while (getline(ss, token, ','))
        {
            tokens.push_back(token);
        }

        if (tokens.size() != 11)
            continue;

        OrderId id = stoull(tokens[0]);
        Side side = (tokens[1] == "BUY") ? Side::BUY : Side::SELL;
        Price price = stoll(tokens[2]);
        Quantity qty = stoll(tokens[3]);

        OrderType type;
        if (tokens[4] == "GTC")
            type = OrderType::GTC;
        else if (tokens[4] == "IOC")
            type = OrderType::IOC;
        else if (tokens[4] == "ICEBERG")
            type = OrderType::ICEBERG;
        else if (tokens[4] == "MARKET")
            type = OrderType::MARKET;
        else if (tokens[4] == "STOP_LOSS")
            type = OrderType::STOP_LOSS;
        else
            type = OrderType::GTC;

        Quantity disp = stoll(tokens[5]);
        Quantity display_size = stoll(tokens[6]);
        uint32_t owner = stoul(tokens[7]);
        Price stop_price = stoll(tokens[8]);
        uint32_t session_id = stoul(tokens[9]);
        string ip_address = tokens[10];

        auto trades = book.add_order(id, side, price, qty, disp, display_size, type, owner, stop_price, session_id);

        if (trades.empty() && (type == OrderType::GTC || type == OrderType::ICEBERG))
        {
            rejected_count++;
        }

        auto order_end = high_resolution_clock::now();
        auto latency = duration_cast<nanoseconds>(order_end - order_start).count();
        latencies.push_back(latency);

        order_count++;
        trade_count += trades.size();

        if (order_count % 1000 == 0)
        {
            Price current_price = book.best_bid() > 0 ? (book.best_bid() + book.best_ask()) / 2 : 100000;
            book.get_risk_manager().mark_to_market(current_price);
        }
    }

    auto end_time = high_resolution_clock::now();
    auto total_time = duration_cast<milliseconds>(end_time - start_time).count();

    sort(latencies.begin(), latencies.end());
    size_t p50_idx = latencies.size() * 0.50;
    size_t p95_idx = latencies.size() * 0.95;
    size_t p99_idx = latencies.size() * 0.99;
    double mean_latency = accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();

    cout << "\n╔══════════════════════════════════════╗" << endl;
    cout << "║      PERFORMANCE REPORT              ║" << endl;
    cout << "╚══════════════════════════════════════╝" << endl;
    cout << "\n Summary Statistics:" << endl;
    cout << "  • Total orders processed: " << order_count << endl;
    cout << "  • Total trades executed: " << trade_count << endl;
    cout << "  • Orders rejected: " << rejected_count << endl;
    cout << "  • Match rate: " << fixed << setprecision(1)
         << (trade_count * 100.0 / order_count) << "%" << endl;
    cout << "  • Total time: " << total_time << " ms" << endl;
    cout << "  • Throughput: " << (order_count * 1000 / max<long long>(total_time, 1LL))
         << " orders/sec" << endl;

    cout << "\n⚡ Latency Analysis (per order):" << endl;
    cout << "  • Mean: " << fixed << setprecision(1) << mean_latency / 1000.0 << " μs" << endl;
    cout << "  • P50:  " << latencies[p50_idx] / 1000.0 << " μs" << endl;
    cout << "  • P95:  " << latencies[p95_idx] / 1000.0 << " μs" << endl;
    cout << "  • P99:  " << latencies[p99_idx] / 1000.0 << " μs" << endl;
    cout << "  • Min:  " << latencies.front() / 1000.0 << " μs" << endl;
    cout << "  • Max:  " << latencies.back() / 1000.0 << " μs" << endl;

    cout << "\n Memory Pool Performance:" << endl;
    book.print_pool_stats();

    cout << "\n════════════════════════════════════════" << endl;
    book.print_stats();
}

int main(int argc, char *argv[])
{
    cout << "==================================================" << endl;

    if (argc == 1)
    {

        string demo_file = "market_orders.csv";
        generate_test_data(demo_file, 50000);
        run_benchmark(demo_file);
        return 0;
    }

    string command = argv[1];

    if (command == "generate" && argc == 4)
    {
        generate_test_data(argv[2], stoull(argv[3]));
        cout << " Market data generated." << endl;
    }
    else if (command == "run" && argc == 3)
    {
        run_benchmark(argv[2]);
    }
    else
    {
        cerr << "Invalid arguments. Try './matching_engine help'" << endl;
        return 1;
    }

    return 0;
}