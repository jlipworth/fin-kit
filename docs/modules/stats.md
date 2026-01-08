# finkit.stats Module

Statistical functions for time series analysis, covariance estimation, and
signal quality assessment.

## Overview

The `finkit.stats` module provides efficient rolling window calculations,
covariance/correlation matrix estimation, and trading signal analysis tools.
It is organized into three partitions:

- `:rolling` - Rolling window statistics
- `:covariance` - Covariance and correlation matrices
- `:signals` - Signal quality metrics

## Rolling Statistics

### RollingAccumulator

Single-pass rolling statistics using Welford's algorithm:

```cpp
class RollingAccumulator {
    explicit RollingAccumulator(size_t window_size);
    void push(double x);
    auto mean() const -> double;
    auto variance() const -> double;
    auto std_dev() const -> double;
    auto min() const -> double;
    auto max() const -> double;
};
```

### Rolling Functions

All functions return vectors NaN-padded at the start until window is filled.

```cpp
auto rolling_stats(span<const double> data, size_t window) -> RollingStats;
auto rolling_mean(span<const double> data, size_t window) -> vector<double>;
auto rolling_std(span<const double> data, size_t window) -> vector<double>;
auto rolling_sum(span<const double> data, size_t window) -> vector<double>;
auto rolling_min(span<const double> data, size_t window) -> vector<double>;
auto rolling_max(span<const double> data, size_t window) -> vector<double>;
auto rolling_zscore(span<const double> data, size_t window) -> vector<double>;
auto rolling_percentile_rank(span<const double> data, size_t window) -> vector<double>;
auto ewma(span<const double> data, double span_param) -> vector<double>;
auto ewma_std(span<const double> data, double span_param) -> vector<double>;
```

## Covariance and Correlation

### Matrix Types

```cpp
struct CovarianceMatrix {
    vector<vector<double>> matrix;
    vector<string> symbols;
    auto variance(size_t i) const -> double;
    auto std_dev(size_t i) const -> double;
};

struct CorrelationMatrix { /* similar */ };
```

### Functions

```cpp
auto covariance(span<const double> x, span<const double> y) -> double;
auto correlation(span<const double> x, span<const double> y) -> double;
auto calculate_covariance_matrix(const vector<vector<double>>& returns,
                                  const vector<string>& symbols) -> CovarianceMatrix;
auto calculate_ewma_covariance(const vector<vector<double>>& returns,
                                const vector<string>& symbols,
                                double halflife) -> CovarianceMatrix;
auto to_correlation_matrix(const CovarianceMatrix& cov) -> CorrelationMatrix;
auto shrink_covariance(const CovarianceMatrix& sample_cov,
                       double shrinkage_intensity) -> CovarianceMatrix;
auto rolling_correlation(span<const double> x, span<const double> y,
                         size_t window) -> vector<double>;
auto rolling_beta(span<const double> y, span<const double> x,
                  size_t window) -> vector<double>;
```

## Signal Analysis

### SignalMetrics

```cpp
struct SignalMetrics {
    double hit_rate;            // % correct direction predictions
    double information_coef;    // IC: rank correlation with returns
    double information_ratio;   // IC / std(IC)
    double avg_signal_strength;
    double signal_autocorr;
};
```

### Functions

```cpp
auto calculate_ic(span<const double> signals, span<const double> returns) -> double;
auto calculate_autocorrelation(span<const double> data, size_t lag) -> double;
auto analyze_signal(span<const double> signals, span<const double> returns) -> SignalMetrics;
auto rolling_ic(span<const double> signals, span<const double> returns,
                size_t window) -> vector<double>;
auto analyze_signal_decay(span<const double> signals,
                          const vector<vector<double>>& returns_matrix,
                          span<const int> horizons) -> SignalDecay;
auto calculate_pacf(span<const double> data, size_t max_lag) -> vector<double>;
auto test_stationarity(span<const double> data) -> StationarityResult;
```

## Usage

```cpp
import finkit.stats;

using namespace finkit::stats;

// Rolling statistics
vector<double> prices = { /* ... */ };
auto stats = rolling_stats(prices, 20);
auto zscore = rolling_zscore(prices, 20);

// Covariance matrix
vector<vector<double>> returns = { /* asset returns */ };
vector<string> symbols = {"SPY", "TLT", "GLD"};
auto cov = calculate_covariance_matrix(returns, symbols);
auto corr = to_correlation_matrix(cov);

// Signal analysis
vector<double> signal = { /* ... */ };
vector<double> fwd_returns = { /* ... */ };
auto metrics = analyze_signal(signal, fwd_returns);
```

## Related

- [basis.md](basis.md) - Uses statistics for basis analysis
- [architecture.md](../architecture.md) - Module dependencies
