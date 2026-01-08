# Future Work and Improvements

This document tracks planned improvements, known gaps, and future enhancements for fin-kit.

## Priority 1: Near-Term Improvements

### Concurrency Support
- [ ] Thread pool for parallel calculations
- [ ] Parallel indicator calculation for independent computations
- [ ] Concurrent curve bootstrapping across currencies
- [ ] Parallel strategy signal generation in backtests
- [ ] Execution policy support for STL algorithms

### Precision & Numerics
- [ ] Decimal type for transaction amounts (avoid floating-point rounding)
- [ ] Proper rounding rules for different asset classes
- [ ] Currency-specific decimal places (JPY=0, USD/EUR=2, crypto=8+)
- [ ] Tick size validation for orders
- [ ] Lot size constraints

### Testing & Validation
- [ ] End-to-end backtest integration tests
- [ ] Risk engine test coverage
- [ ] Calculation module unit tests with known results
- [ ] Performance benchmarks

## Priority 2: Framework Enhancements

### Backtest Framework
- [ ] Look-ahead bias detection/prevention
- [ ] Survivorship bias handling (delisted securities)
- [ ] Dynamic universe management (filter by criteria as-of date)
- [ ] Short selling realism (locate, borrow costs, availability)
- [ ] Margin/collateral modeling
- [ ] Settlement cycle modeling (T+1, T+2)
- [ ] Multi-leg orders (spreads, pairs)
- [ ] Corporate actions (splits, dividends, spin-offs)
- [ ] Futures roll handling
- [ ] Options expiration/exercise

### Risk Framework
- [ ] Factor risk model integration (Barra, Axioma style)
- [ ] VaR suite (Historical, Parametric, Monte Carlo)
- [ ] Stress testing framework with predefined scenarios
- [ ] Correlation-based net exposure calculation
- [ ] Liquidity risk (ADV-based position limits)
- [ ] Sector/geography concentration limits
- [ ] Intraday VaR updates
- [ ] Risk attribution (factor contribution)

### Trading Framework
- [ ] Market impact models (square-root, linear)
- [ ] Order queue position modeling for limit orders
- [ ] Exchange rejection simulation
- [ ] Partial fill detailed handling
- [ ] TWAP/VWAP/POV execution algorithms

## Priority 3: Calculation Module Extensions

### Bootstrap Module
- [ ] Dual-curve bootstrapping
- [ ] Spread curves
- [ ] Basis swap curves
- [ ] Cap/floor vol bootstrapping

### Basis Module
- [ ] Roll basis calculations
- [ ] Calendar spreads
- [ ] Swap spreads
- [ ] TRS basis
- [ ] Cross-currency basis term structure

### Valuation Module
- [ ] Option pricing (Black-Scholes, binomial)
- [ ] Callable/putable bonds (OAS)
- [ ] Swaption pricing
- [ ] CDS valuation

### Stats Module
- [ ] EWMA with configurable decay
- [ ] Robust statistics (median, MAD)
- [ ] Regime detection
- [ ] Factor analysis
- [ ] Cointegration tests

## Priority 4: Data Quality & Infrastructure

### Data Quality
- [ ] Bad tick detection and filtering
- [ ] Data staleness alerts
- [ ] Missing data interpolation
- [ ] Data revision tracking
- [ ] Adjusted vs unadjusted price handling

### Market Microstructure
- [ ] Trading halt handling
- [ ] Short sale restriction (SSR/uptick rule)
- [ ] Limit up/limit down bands
- [ ] Odd lot handling
- [ ] Pre/post market trading
- [ ] Auction period modeling

### Instrument Lifecycle
- [ ] IPO handling (securities with no history)
- [ ] Delisting handling
- [ ] Ticker changes
- [ ] Mergers and acquisitions
- [ ] Bankruptcies
- [ ] Rights issues
- [ ] Bond maturity

## Priority 5: Long-Term Vision

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
- [ ] SIMD optimization for calculations
- [ ] GPU acceleration for large matrix operations
- [ ] Memory-mapped large dataset support
- [ ] Incremental calculation updates

## Known Gaps from Quant Review

These gaps were identified during architecture review and should be addressed:

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
