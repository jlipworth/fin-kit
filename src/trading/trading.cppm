/// @file trading.cppm
/// @brief Trading module - main interface
///
/// This module provides order management, execution simulation, and
/// position tracking for backtesting and trading.
///
/// Module partitions:
/// - finkit.trading:types  - Order, Fill, Position, Instrument types
/// - finkit.trading:engine - IExecutionEngine and BacktestExecutionEngine
///
/// @see docs/frameworks/trading.md for documentation

module;

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module finkit.trading;

export import :types;
export import :engine;
