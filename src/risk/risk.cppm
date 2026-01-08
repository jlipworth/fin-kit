/// @file risk.cppm
/// @brief Risk module - main interface
///
/// This module provides risk management for backtesting and trading,
/// including pre-trade checks, post-trade monitoring, and active position
/// management.
///
/// Module partitions:
/// - finkit.risk:types  - Limit structures, breach policies, events
/// - finkit.risk:engine - IRiskEngine and StandardRiskEngine
///
/// @see docs/frameworks/risk.md for documentation

module;

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

export module finkit.risk;

export import :types;
export import :engine;
