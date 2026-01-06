# Roadmap

## Phase 1: Foundation

- [x] Project structure and build system
- [ ] Core time series container
- [ ] DateTime with timezone support
- [ ] Fixed-point decimal type
- [ ] Result type for error handling
- [ ] Unit test infrastructure

## Phase 2: Data Layer

- [ ] Bar (OHLCV) data structures
- [ ] Tick data structures
- [ ] CSV data source
- [ ] Memory-mapped data store
- [ ] Data normalization utilities

## Phase 3: Analysis

- [ ] Indicator base class
- [ ] Moving averages (SMA, EMA, WMA)
- [ ] Momentum indicators (RSI, MACD, Stochastic)
- [ ] Volatility indicators (ATR, Bollinger Bands)
- [ ] Rolling statistics
- [ ] Signal generation framework

## Phase 4: Backtesting

- [ ] Event-driven engine core
- [ ] Strategy interface
- [ ] Portfolio management
- [ ] Order types (market, limit, stop)
- [ ] Execution simulation
- [ ] Slippage and commission models
- [ ] Risk controls

## Phase 5: Visualization

- [ ] Terminal-based price charts
- [ ] Performance metrics display
- [ ] Trade log formatting
- [ ] Equity curve plotting

## Phase 6: Advanced Features

- [ ] Multi-asset support
- [ ] Walk-forward optimization
- [ ] Monte Carlo analysis
- [ ] Parameter sensitivity analysis
- [ ] Live trading adapter interface

## Future Considerations

- Python bindings via pybind11
- WebSocket data feeds
- Database integration (TimescaleDB, QuestDB)
- Cloud deployment support
