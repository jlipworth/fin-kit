/// @file valuation.cppm
/// @brief Valuation module - main interface
///
/// This module provides fair value calculations for financial instruments.
///
/// Module partitions:
/// - finkit.valuation:bond - Bond pricing, yields, duration, relative value
/// - finkit.valuation:swap - OIS swap pricing, par rates, DV01
///
/// @see docs/modules/valuation.md for documentation

module;

#include <ql/quantlib.hpp>
#include <string>
#include <vector>

export module finkit.valuation;

export import :bond;
export import :swap;
