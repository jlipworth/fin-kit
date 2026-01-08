/// @file backtest.cppm
/// @brief Backtesting framework module
///
/// The backtest module provides a complete simulation framework for testing
/// trading strategies against historical data. It includes:
/// - Portfolio management with multi-currency support
/// - Strategy interface with lifecycle callbacks
/// - Backtest engine with configurable execution
/// - Integration with risk management

export module finkit.backtest;

export import :types;
export import :strategy;
export import :engine;
