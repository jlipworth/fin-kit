# Roadmap

## Phase 1: Foundation (Complete)

- [x] Project structure and C++20 modules build system
- [x] Core module (paths, logging)
- [x] Data module with InputDataStore/OutputDataStore
- [x] Types module with shared financial types
- [x] Unit test infrastructure (GoogleTest)

## Phase 2: Calculation Libraries (Complete)

- [x] Bootstrap module (SOFR curve, OIS, CB cut probabilities)
- [x] Basis module (bond basis, CIP basis, index futures)
- [x] Stats module (rolling statistics, covariance, signal analysis)
- [x] Valuation module (bond valuation, swap valuation)
- [x] Curves module (rate accessors, CIP utilities)
- [x] Analysis module (bond basket analysis)

## Phase 3: Frameworks (Complete)

- [x] Trading module (Order/Fill types, IExecutionEngine, fill models)
- [x] Risk module (limits, pre-trade checks, active monitoring)
- [x] Backtest module (engine, IStrategy, Portfolio)

## Phase 4: Visualization (In Progress)

- [ ] Viz module implementation
- [ ] Terminal-based charts
- [ ] Performance metrics display
- [ ] Equity curve plotting

## Phase 5: Testing & Validation (In Progress)

- [ ] Mock data with expected results
- [ ] End-to-end backtest tests
- [ ] Risk engine test coverage
- [ ] Calculation module test coverage

## Phase 6: Advanced Features (Planned)

### Concurrency
- [ ] Parallel indicator calculation for independent computations
- [ ] Concurrent curve bootstrapping across currencies
- [ ] Parallel strategy signal generation
- [ ] Thread pool for backtest calculation modules

### Precision & Numerics
- [ ] Decimal type for transaction amounts (avoid floating-point rounding)
- [ ] Proper rounding rules for different asset classes (penny stocks, futures tick sizes)
- [ ] Currency-specific decimal places (JPY=0, most=2, crypto=8+)

### Risk Enhancements
- [ ] Factor risk model integration (Barra, Axioma)
- [ ] VaR suite (Historical, Parametric, Monte Carlo)
- [ ] Stress testing framework
- [ ] Correlation-based net exposure

### Trading Enhancements
- [ ] Market impact models (square-root, linear)
- [ ] Order queue position modeling
- [ ] Partial fill handling
- [ ] Multi-leg orders (spreads, pairs)

### Data Quality
- [ ] Look-ahead bias detection
- [ ] Survivorship bias handling
- [ ] Bad tick filtering
- [ ] Data staleness alerts

### Instrument Lifecycle
- [ ] Corporate actions (splits, dividends, spin-offs)
- [ ] Futures roll handling
- [ ] Options expiration/exercise
- [ ] Bond maturity

## Future Considerations

- Python bindings via pybind11
- Live trading adapter interface
- WebSocket data feeds
- Walk-forward optimization
- Monte Carlo scenario analysis
- Multi-strategy framework with shared capital
