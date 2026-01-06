# Architecture

## Overview

fin-kit is a modular C++20 financial analysis toolkit designed for quantitative research and backtesting. The architecture emphasizes:

- **Zero-cost abstractions**: Performance-critical paths use compile-time polymorphism
- **Modularity**: Each component is independently buildable and testable
- **Type safety**: Strong typing for financial concepts (prices, quantities, timestamps)

## Module Dependency Graph

```
┌─────────┐
│   viz   │
└────┬────┘
     │
┌────▼────┐
│backtest │
└────┬────┘
     │
┌────▼────┐
│analysis │
└────┬────┘
     │
┌────▼────┐
│  data   │
└────┬────┘
     │
┌────▼────┐
│  core   │
└─────────┘
```

## Module Descriptions

### core

Foundation library providing:

- **TimeSeries<T>**: Generic time-indexed container with O(1) append, O(log n) lookup
- **DateTime**: Nanosecond-precision timestamps with timezone support
- **Decimal**: Fixed-point arithmetic for financial calculations
- **Result<T, E>**: Error handling without exceptions

### data

Market data handling:

- **DataSource**: Abstract interface for data providers
- **Bar**: OHLCV candlestick representation
- **Tick**: Individual trade/quote records
- **DataStore**: On-disk storage with memory-mapped access

### analysis

Technical analysis and statistics:

- **Indicator<T>**: Base class for streaming indicators
- **MovingAverage, RSI, MACD, etc.**: Common technical indicators
- **Statistics**: Rolling statistics, correlation, regression
- **Signal**: Signal generation and combination

### backtest

Event-driven backtesting engine:

- **Engine**: Main simulation loop with event dispatch
- **Strategy**: User-defined trading logic interface
- **Portfolio**: Position and P&L tracking
- **Execution**: Order matching and fill simulation
- **Risk**: Position limits, drawdown controls

### viz

Terminal visualization:

- **Chart**: ASCII/Unicode price charts
- **Table**: Formatted data tables
- **Report**: Performance reporting

## Threading Model

- Single-threaded by default for deterministic backtests
- Optional parallel indicator calculation via thread pool
- Lock-free queues for data ingestion

## Memory Management

- Arena allocators for temporary calculations
- Memory-mapped files for large datasets
- RAII throughout, no raw new/delete

## Error Handling

- `Result<T, E>` for recoverable errors
- Assertions for invariant violations
- No exceptions in hot paths
