# Architecture

## Overview

fin-kit is a modular C++20 financial analysis toolkit designed for quantitative research and backtesting. The architecture emphasizes:

> **Looking for setup instead?** This file describes the system design, not the
> installation flow. For first-time setup use [SETUP.md](SETUP.md); for Redis,
> TimescaleDB, Docker Desktop, and WSL-backed local workflows use
> [LOCAL_SERVICES.md](LOCAL_SERVICES.md).

- **Zero-cost abstractions**: Performance-critical paths use compile-time polymorphism
- **Modularity**: Each component is independently buildable and testable
- **Type safety**: Strong typing for financial concepts (prices, quantities, timestamps)
- **Separation of concerns**: Clear boundaries between data, calculations, and frameworks

## Design Principles

### Calculation Modules are Pure
- No framework imports
- Input -> Output functions
- Stateless
- Can be used standalone (CLI, notebooks)

### Frameworks Compose Modules
- Load data from DataStore
- Call calculation module functions
- Write results to DataStore
- No circular dependencies

### Conventions as Parameters
- All calculations take conventions as parameters (day count, calendar, business day convention)
- Never hardcoded locale or market assumptions
- ConventionRegistry provides convenience lookups only

### Outsource Math to Libraries
The engines and frameworks are already complicated. Outsource all strong math (where mistakes could happen) to battle-tested libraries:

| Domain | Library | Examples |
|--------|---------|----------|
| Linear algebra | **Eigen** | Covariance matrices, factor models, Cholesky decomposition |
| Interest rates | **QuantLib** | Curve bootstrapping, bond pricing, day counting |
| Statistics | **Eigen + std** | Mean, variance via Eigen; sorting/searching via `<algorithm>` |
| Data storage | **libpqxx/TimescaleDB** | SQL queries, time series, shared data store |

**Why:** A bug in a hand-rolled covariance calculation is hard to find. A bug in framework orchestration is easier to debug. Let libraries handle the math correctness; focus custom code on domain-specific orchestration.

## Module Dependency Graph

```
                          ┌───────────┐
                          │    viz    │ <-- Reads output tables
                          └─────┬─────┘
                                │
            ┌───────────────────┼───────────────────┐
            │                   │                   │
      ┌─────▼─────┐       ┌─────▼─────┐       ┌─────▼─────┐
      │ backtest  │       │   risk    │       │  trading  │
      └─────┬─────┘       └─────┬─────┘       └─────┬─────┘
            │                   │                   │
            └───────────────────┴───────────────────┘
                                │
     ┌──────────┬──────────┬────┴────┬──────────┬──────────┐
     │          │          │         │          │          │
┌────▼───┐ ┌────▼────┐ ┌───▼───┐ ┌───▼───┐ ┌────▼────┐ ┌───▼────┐
│bootstrap│ │  basis  │ │curves │ │analysis│ │valuation│ │ stats  │
└────┬───┘ └────┬────┘ └───┬───┘ └───┬───┘ └────┬────┘ └───┬────┘
     │          │          │         │          │          │
     └──────────┴──────────┴────┬────┴──────────┴──────────┘
                                │
                          ┌─────▼─────┐
                          │   types   │
                          └─────┬─────┘
                                │
                          ┌─────▼─────┐
                          │   data    │
                          └─────┬─────┘
                                │
                          ┌─────▼─────┐
                          │   core    │
                          └───────────┘
```

## Layer Architecture

### Layer 1: Foundation
- **finkit.core**: Path utilities, logging, DateTime, Decimal, Result<T,E>

### Layer 2: Data Access
- **finkit.data**: Database connection, config loading
  - DataStore: Unified read/write access to TimescaleDB (shared with Python repo)

### Layer 3: Shared Types
- **finkit.types**: Shared financial types
  - Currency, OvernightIndex enums
  - CurrencyConvention
  - FXSpot, FXForward, InterestRate, XCCYBasisSwap
  - Bond, FuturesContract, RepoRate
  - OptionQuote, VolSurface

### Layer 4: Calculation Libraries
| Module | Purpose | Key Functions |
|--------|---------|---------------|
| `finkit.bootstrap` | Curve construction | `bootstrap_sofr_curve()`, `bootstrap_ois_curve()` |
| `finkit.basis` | Basis calculations | `bond_basis()`, `index_future_basis()`, `cip_basis()` |
| `finkit.stats` | Statistics | `rolling_stats()`, `covariance()`, `signal_analysis()` |
| `finkit.valuation` | Fair value | `value_bond()`, `par_rate()`, `value_swap()` |
| `finkit.curves` | Curve utilities | Rate accessors, CIP basis |
| `finkit.analysis` | Bond analysis | Basket analysis, ranking |

### Layer 5: Frameworks
| Module | Purpose | Description |
|--------|---------|-------------|
| `finkit.trading` | Order/execution | Order types, fill models, execution engine interface |
| `finkit.risk` | Risk management | Limits, pre-trade checks, active monitoring |
| `finkit.backtest` | Simulation | Strategy interface, portfolio, backtest engine |

### Layer 6: Presentation
- **finkit.viz**: Terminal visualization, charts, reports

## Module Descriptions

### core
Foundation library providing:
- **TimeSeries<T>**: Generic time-indexed container
- **DateTime**: Nanosecond-precision timestamps
- **Decimal**: Fixed-point arithmetic for financial calculations
- **Result<T, E>**: Error handling without exceptions

### data
Data handling with unified access:
- **DataStore**: Unified access to TimescaleDB for market data (reads) and calculation results (writes)
- **Config**: TOML configuration loading

### types
Shared financial types used across all modules:
- **Currency**: G10 currency enumeration (USD, EUR, GBP, JPY, etc.)
- **OvernightIndex**: SOFR, ESTR, SONIA, TONAR, etc.
- **Bond**: CUSIP, coupon, maturity, price
- **FXSpot/FXForward**: FX market data types

### bootstrap
Curve construction:
- SOFR OIS curve bootstrapping
- Central bank cut probability calculation
- Interpolation methods (log-linear, cubic spline)

### basis
Basis and spread calculations:
- **Bond basis**: Gross/net basis, implied repo, CTD identification
- **CIP basis**: Cross-currency basis vs OIS
- **Index futures basis**: Fair value vs market

### stats
Statistical functions:
- Rolling statistics (mean, std, z-score, EWMA)
- Covariance/correlation matrices
- Signal analysis (IC, hit ratio, turnover)

### valuation
Fair value calculations:
- Bond valuation (clean/dirty price, yield, z-spread)
- Swap valuation (par rate, NPV, DV01)

### trading
Order and execution management:
- **Order/Fill types**: Market, limit, stop orders
- **IExecutionEngine**: Swappable execution (backtest, paper, live)
- **IFillModel**: CloseFill, NextBarOpen, VWAP models

### risk
Risk management framework:
- **Pre-trade checks**: Position limits, concentration
- **Active monitoring**: Drawdown, exposure, VaR limits
- **Breach policies**: Reject, reduce, liquidate, halt

### backtest
Event-driven backtesting:
- **BacktestEngine**: Main simulation loop
- **IStrategy**: User-defined trading logic interface
- **Portfolio**: Multi-currency position and P&L tracking
- **IDataFeed**: Historical data feed interface

### viz
Visualization (placeholder):
- Charts, tables, performance reports

## Threading Model

- Single-threaded by default for deterministic backtests
- Future: Parallel calculation support for independent computations
- Lock-free queues for data ingestion

## Memory Management

- Arena allocators for temporary calculations
- PostgreSQL/TimescaleDB for persistent data storage
- RAII throughout, no raw new/delete

## Error Handling

- `std::optional` for absent values
- Exceptions for unrecoverable errors only
- Assertions for invariant violations

## Data Flow

```
Input Tables ──► Calculation Modules ──► Output Tables ──► Viz
     │                   │                     │
     │                   ▼                     │
     │            Frameworks (backtest,        │
     └───────────► risk, trading) ─────────────┘
```

## Key Interfaces

### IInstrument
Abstract instrument interface for asset-class agnostic design:
```cpp
class IInstrument {
    virtual auto symbol() const -> string = 0;
    virtual auto asset_class() const -> AssetClass = 0;
    virtual auto currency() const -> Currency = 0;
    virtual auto multiplier() const -> double = 0;
};
```

### IExecutionEngine
Swappable execution:
```cpp
class IExecutionEngine {
    virtual auto submit_order(const Order& order) -> OrderId = 0;
    virtual auto cancel_order(OrderId id) -> bool = 0;
    virtual void on_bar(const BarEvent& bar) = 0;
};
```

### IRiskEngine
Bi-directional risk management:
```cpp
class IRiskEngine {
    // Mode 1: Pre-trade approval (trading engine asks)
    virtual auto check_pre_trade(const Order&, const Portfolio&, const IFXRateProvider&) -> RiskCheckResult = 0;

    // Mode 2: Active monitoring (risk engine watches)
    virtual auto monitor(const Portfolio&, const IFXRateProvider&, Timestamp) -> vector<RiskActionEvent> = 0;
};
```

### IStrategy
User-implemented trading logic:
```cpp
class IStrategy {
    virtual auto name() const -> string = 0;
    virtual void on_bar(const BarEvent& bar, StrategyContext& ctx) = 0;
    virtual void on_fill(const Fill& fill, StrategyContext& ctx) {}
    virtual void on_risk_action(const RiskActionEvent& event, StrategyContext& ctx) {}
};
```
