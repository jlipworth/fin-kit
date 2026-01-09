/// @file stats.cppm
/// @brief Statistics module - main interface
///
/// This module provides statistical functions for time series analysis,
/// covariance estimation, and signal quality assessment.
///
/// Module partitions:
/// - finkit.stats:rolling    - Rolling window statistics
/// - finkit.stats:covariance - Covariance/correlation matrices
/// - finkit.stats:signals    - Signal analysis and quality metrics
/// - finkit.stats:returns    - Return-based performance metrics
///
/// @see docs/modules/stats.md for documentation

module;

#include <span>
#include <string>
#include <vector>

export module finkit.stats;

export import :rolling;
export import :covariance;
export import :signals;
export import :returns;
