# Learning-Focused Trading Engine Simulation

A C++ order matching engine built to understand high-frequency trading system design. Features market simulation and risk management concepts commonly found in quantitative finance.

## Project Background

Created as a learning exercise to explore trading system architecture and practice low-latency C++ programming techniques. Attempts to model realistic market behavior while implementing fundamental risk controls in a simplified simulation environment.

## Features

**Core Matching Logic:**
- Implements 6 order types: GTC, IOC, FOK, MARKET, ICEBERG, STOP_LOSS
- Price-time priority matching with simple rebate modeling
- Zero-allocation memory pools to minimize allocations during execution
- Attempts NYSE/NASDAQ tick size compliance

**Risk Management Components:**
- Position tracking and P&L calculation
- Fat finger protection and circuit breaker logic
- Session-based rate limiting
- Risk limits and self-trade prevention

**Market Simulation:**
- Trader behavior models (MM, HFT, Institutional, Retail)
- Price discovery with volatility patterns
- Time-based volume simulation
- Elementary market microstructure modeling

## Observed Performance

*Note: All measurements from my development environment with synthetic test data*

**Test Run Results (50K orders):**
- **Throughput:** ~1.14M orders/sec (44ms total processing time)
- **Match Rate:** 33.1% (16,564 trades from 50,000 orders)
- **Rejections:** 8,693 orders (risk limits, invalid prices, etc.)

**Latency Distribution (per order):**
- **Mean:** 0.7 μs, **P50:** 0.6 μs
- **P95:** 0.9 μs, **P99:** 1.2 μs  
- **Range:** 0.5 μs (min) to 14.5 μs (max)

**Memory Efficiency:**
- **Order Pool:** 0.2% utilization (3,085 / 2M capacity)
- **Trade Pool:** 0.0% utilization (minimal usage)
- Minimal heap allocations during matching

*Results will vary significantly based on hardware, compiler settings, and order patterns*

## Quick Start

```bash
# Build with optimizations
make clean && make

# Generate market data and run stimulations
make run
```

## Architecture

```
Core Components:
├── types.h              # Order/trade data structures
├── order_book.h/.cpp    # Lock-free matching engine
├── risk_manager.h       # Position & risk controls
├── market_sim.h/.cpp    # Realistic order generation
├── tick_table.h         # Exchange-compliant pricing
├── object_pool.h        # Zero-allocation memory management
└── session_mgr.h       # Connection & authentication
```

## Sample Output

```
╔══════════════════════════════════════╗
║      SIMULATION RESULTS              ║
╚══════════════════════════════════════╝

Summary Statistics:
  • Total orders processed: 50,000
  • Total trades executed: 16,564
  • Orders rejected: 8,693
  • Match rate: 33.1%
  • Total time: 44 ms
  • Throughput: 1,136,363 orders/sec

Latency Analysis (per order):
  • Mean: 0.7 μs  • P50: 0.6 μs  • P95: 0.9 μs
  • P99: 1.2 μs   • Min: 0.5 μs  • Max: 15.2 μs

Memory Pool Performance:
  • Order Pool: 0.2% utilization (3,085 / 2M capacity)
  • Trade Pool: 0.0% utilization
  • Minimal heap allocations during execution

Note: Results from development machine with synthetic data
```

## Technical Approach

- **Lock-free design** using STL containers for price-time priority
- **Memory management** with pre-allocated object pools  
- **Elementary market modeling** including spread dynamics and volume patterns
- **Risk controls** to prevent common issues in trading systems
- **Microsecond timing** using RDTSC for latency measurement

## What I Learned

This project helped me explore:
- Principles of low-latency system design
- Fundamentals of financial market structure
- Memory-efficient C++ programming practices  
- Risk management concepts
- Performance measurement techniques
- Software architecture for financial applications

While this simulation is quite simplified compared to real trading systems, it provided valuable hands-on experience with concepts used in quantitative finance.

---

**Note:** This is a learning project and simulation built for educational purposes. While I've tried to incorporate realistic concepts, it's simplified compared to actual production trading systems. All performance measurements are from my development environment with synthetic test data.

## License

MIT License - see LICENSE file for details