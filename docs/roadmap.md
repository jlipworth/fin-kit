# Roadmap

This document tracks the development roadmap for fin-kit, including completed work, current priorities, and long-term vision.

---

## Completed Phases

### Phase 1: Foundation

- [x] Project structure and C++20 modules build system
- [x] Core module (paths, logging)
- [x] Data module with InputDataStore/OutputDataStore
- [x] Types module with shared financial types
- [x] Unit test infrastructure (GoogleTest)

### Phase 2: Calculation Libraries

- [x] Bootstrap module (SOFR curve, OIS, CB cut probabilities)
- [x] Basis module (bond basis, CIP basis, index futures)
- [x] Stats module (rolling statistics, covariance, signal analysis)
- [x] Valuation module (bond valuation, swap valuation)
- [x] Curves module (rate accessors, CIP utilities)
- [x] Analysis module (bond basket analysis)

### Phase 3: Frameworks

- [x] Trading module (Order/Fill types, IExecutionEngine, fill models)
- [x] Risk module (limits, pre-trade checks, active monitoring)
- [x] Backtest module (engine, IStrategy, Portfolio)

---

## Current Phase

### Phase 4: Visualization

- [ ] Viz module implementation
- [ ] Terminal-based charts
- [ ] Performance metrics display
- [ ] Equity curve plotting

### Phase 5: Testing & Validation

- [ ] Mock data with expected results
- [ ] End-to-end backtest integration tests
- [ ] Risk engine test coverage
- [ ] Calculation module unit tests with known results
- [ ] Performance benchmarks

---

## Planned Phases

### Phase 6: Concurrency & Performance

**Concurrency Support**
- [ ] Thread pool for parallel calculations
- [ ] Parallel indicator calculation for independent computations
- [ ] Concurrent curve bootstrapping across currencies
- [ ] Parallel strategy signal generation in backtests
- [ ] Execution policy support for STL algorithms

**Performance Optimization**
- [ ] SIMD optimization for calculations
- [ ] Memory-mapped large dataset support
- [ ] Incremental calculation updates

### Phase 7: Precision & Numerics

- [ ] Decimal type for transaction amounts (avoid floating-point rounding)
- [ ] Proper rounding rules for different asset classes (penny stocks, futures tick sizes)
- [ ] Currency-specific decimal places (JPY=0, USD/EUR=2, crypto=8+)
- [ ] Tick size validation for orders
- [ ] Lot size constraints

### Phase 8: Risk Framework Enhancements

- [ ] Factor risk model integration (Barra, Axioma style)
- [ ] VaR suite (Historical, Parametric, Monte Carlo)
- [ ] Stress testing framework with predefined scenarios
- [ ] Correlation-based net exposure calculation
- [ ] Liquidity risk (ADV-based position limits)
- [ ] Sector/geography concentration limits
- [ ] Intraday VaR updates
- [ ] Risk attribution (factor contribution)

### Phase 9: Trading Framework Enhancements

- [ ] Market impact models (square-root, linear)
- [ ] Order queue position modeling for limit orders
- [ ] Exchange rejection simulation
- [ ] Partial fill detailed handling
- [ ] TWAP/VWAP/POV execution algorithms
- [ ] Multi-leg orders (spreads, pairs)

### Phase 10: Backtest Framework Enhancements

- [ ] Look-ahead bias detection/prevention
- [ ] Survivorship bias handling (delisted securities)
- [ ] Dynamic universe management (filter by criteria as-of date)
- [ ] Short selling realism (locate, borrow costs, availability)
- [ ] Margin/collateral modeling
- [ ] Settlement cycle modeling (T+1, T+2)
- [ ] Corporate actions (splits, dividends, spin-offs)
- [ ] Futures roll handling
- [ ] Options expiration/exercise

### Phase 11: Calculation Module Extensions

**Bootstrap Module**
- [ ] Dual-curve bootstrapping
- [ ] Spread curves
- [ ] Basis swap curves
- [ ] Cap/floor vol bootstrapping

**Basis Module**
- [ ] Roll basis calculations
- [ ] Calendar spreads
- [ ] Swap spreads
- [ ] TRS basis
- [ ] Cross-currency basis term structure

**Valuation Module**
- [ ] Option pricing (Black-Scholes, binomial)
- [ ] Callable/putable bonds (OAS)
- [ ] Swaption pricing
- [ ] CDS valuation

**Stats Module**
- [ ] EWMA with configurable decay
- [ ] Robust statistics (median, MAD)
- [ ] Regime detection
- [ ] Factor analysis
- [ ] Cointegration tests

### Phase 12: Data Quality & Infrastructure

**Data Quality**
- [ ] Bad tick detection and filtering
- [ ] Data staleness alerts
- [ ] Missing data interpolation
- [ ] Data revision tracking
- [ ] Adjusted vs unadjusted price handling

**Market Microstructure**
- [ ] Trading halt handling
- [ ] Short sale restriction (SSR/uptick rule)
- [ ] Limit up/limit down bands
- [ ] Odd lot handling
- [ ] Pre/post market trading
- [ ] Auction period modeling

**Instrument Lifecycle**
- [ ] IPO handling (securities with no history)
- [ ] Delisting handling
- [ ] Ticker changes
- [ ] Mergers and acquisitions
- [ ] Bankruptcies
- [ ] Rights issues
- [ ] Bond maturity

---

## Long-Term Vision

### Architecture
- [ ] Walk-forward optimization framework
- [ ] Monte Carlo scenario simulation
- [ ] Multi-strategy framework with shared capital
- [ ] Cross-strategy risk management
- [ ] Real-time risk monitoring

### Integration
- [ ] Python bindings (pybind11)
- [ ] Live trading adapter interface
- [ ] WebSocket data feeds
- [ ] Database backends (TimescaleDB, QuestDB)
- [ ] Cloud deployment support

### Performance
- [ ] GPU acceleration for large matrix operations

---

## Known Critical Gaps

These gaps were identified during architecture review and should be prioritized:

| Gap | Impact | Module |
|-----|--------|--------|
| Look-ahead bias prevention | Critical | backtest |
| Survivorship bias | Critical | backtest |
| Liquidity risk | Critical | risk |
| Factor risk models | High | risk |
| Market impact model | High | trading |
| Settlement cycles | Moderate | trading |
| Corporate actions | Moderate | backtest |
| Bad tick detection | Moderate | data |

---

## Implementation Notes

### Concurrency Strategy
When implementing concurrency:
1. Start with embarrassingly parallel operations (multi-symbol calculations)
2. Use std::jthread for managed threads
3. Consider std::execution policies for STL algorithms
4. Use thread-safe data structures or minimize shared state
5. Profile before optimizing

### Decimal Implementation Strategy
When implementing decimal precision:
1. Evaluate boost::multiprecision vs custom fixed-point
2. Define precision requirements per asset class
3. Implement rounding rules at trade execution
4. Consider performance impact on hot paths
5. Maintain backward compatibility with double API

---

*Last updated: 2026-01-08*
