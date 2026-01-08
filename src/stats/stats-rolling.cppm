/// @file stats-rolling.cppm
/// @brief Rolling window statistics calculations
///
/// Provides efficient rolling window calculations for time series analysis.
/// All functions operate on vectors of doubles and return results aligned
/// with the input (NaN for insufficient data at the start).

module;

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <numeric>
#include <span>
#include <string>
#include <vector>

export module finkit.stats:rolling;

export namespace finkit::stats {

using std::span;
using std::string;
using std::vector;

/// Result of rolling statistics calculation
struct RollingStats {
    vector<double> mean;
    vector<double> std_dev;
    vector<double> variance;
    vector<double> skewness;
    vector<double> kurtosis;
    vector<double> min;
    vector<double> max;
    size_t window_size;
};

/// Single-pass rolling statistics using Welford's algorithm
class RollingAccumulator {
public:
    explicit RollingAccumulator(size_t window_size)
        : window_size_(window_size), values_(window_size) {}

    void push(double x) {
        if (count_ < window_size_) {
            // Filling initial window
            values_[count_] = x;
            ++count_;
            update_stats_add(x);
        } else {
            // Rolling window - remove oldest, add newest
            double old_val = values_[pos_];
            values_[pos_] = x;
            pos_ = (pos_ + 1) % window_size_;
            update_stats_replace(old_val, x);
        }
    }

    [[nodiscard]] auto mean() const -> double {
        return count_ > 0 ? mean_ : std::numeric_limits<double>::quiet_NaN();
    }

    [[nodiscard]] auto variance() const -> double {
        if (count_ < 2)
            return std::numeric_limits<double>::quiet_NaN();
        return m2_ / static_cast<double>(count_ - 1);
    }

    [[nodiscard]] auto std_dev() const -> double { return std::sqrt(variance()); }

    [[nodiscard]] auto min() const -> double {
        if (count_ == 0)
            return std::numeric_limits<double>::quiet_NaN();
        return *std::min_element(values_.begin(), values_.begin() + static_cast<ptrdiff_t>(count_));
    }

    [[nodiscard]] auto max() const -> double {
        if (count_ == 0)
            return std::numeric_limits<double>::quiet_NaN();
        return *std::max_element(values_.begin(), values_.begin() + static_cast<ptrdiff_t>(count_));
    }

    [[nodiscard]] auto count() const -> size_t { return count_; }
    [[nodiscard]] auto is_full() const -> bool { return count_ >= window_size_; }

private:
    void update_stats_add(double x) {
        double delta = x - mean_;
        mean_ += delta / static_cast<double>(count_);
        double delta2 = x - mean_;
        m2_ += delta * delta2;
    }

    void update_stats_replace(double old_val, double new_val) {
        // Update mean
        double delta = (new_val - old_val) / static_cast<double>(window_size_);
        mean_ += delta;

        // Recalculate M2 (variance * (n-1)) from scratch for accuracy
        m2_ = 0.0;
        for (size_t i = 0; i < window_size_; ++i) {
            double d = values_[i] - mean_;
            m2_ += d * d;
        }
    }

    size_t window_size_;
    vector<double> values_;
    size_t count_{0};
    size_t pos_{0};
    double mean_{0.0};
    double m2_{0.0};
};

/// Calculate rolling statistics for a time series
/// @param data Input time series
/// @param window Rolling window size
/// @return RollingStats with all statistics, NaN-padded at start
auto rolling_stats(span<const double> data, size_t window) -> RollingStats {
    RollingStats result;
    result.window_size = window;

    const size_t n = data.size();
    result.mean.resize(n);
    result.std_dev.resize(n);
    result.variance.resize(n);
    result.min.resize(n);
    result.max.resize(n);

    RollingAccumulator acc(window);
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    for (size_t i = 0; i < n; ++i) {
        acc.push(data[i]);

        if (acc.is_full()) {
            result.mean[i] = acc.mean();
            result.variance[i] = acc.variance();
            result.std_dev[i] = acc.std_dev();
            result.min[i] = acc.min();
            result.max[i] = acc.max();
        } else {
            result.mean[i] = nan;
            result.variance[i] = nan;
            result.std_dev[i] = nan;
            result.min[i] = nan;
            result.max[i] = nan;
        }
    }

    return result;
}

/// Calculate rolling mean only (more efficient than full rolling_stats)
auto rolling_mean(span<const double> data, size_t window) -> vector<double> {
    const size_t n = data.size();
    vector<double> result(n, std::numeric_limits<double>::quiet_NaN());

    if (n < window || window == 0)
        return result;

    // Calculate first window sum
    double sum = 0.0;
    for (size_t i = 0; i < window; ++i) {
        sum += data[i];
    }
    result[window - 1] = sum / static_cast<double>(window);

    // Rolling update
    for (size_t i = window; i < n; ++i) {
        sum += data[i] - data[i - window];
        result[i] = sum / static_cast<double>(window);
    }

    return result;
}

/// Calculate rolling standard deviation
auto rolling_std(span<const double> data, size_t window) -> vector<double> {
    auto stats = rolling_stats(data, window);
    return std::move(stats.std_dev);
}

/// Calculate rolling sum
auto rolling_sum(span<const double> data, size_t window) -> vector<double> {
    const size_t n = data.size();
    vector<double> result(n, std::numeric_limits<double>::quiet_NaN());

    if (n < window || window == 0)
        return result;

    double sum = 0.0;
    for (size_t i = 0; i < window; ++i) {
        sum += data[i];
    }
    result[window - 1] = sum;

    for (size_t i = window; i < n; ++i) {
        sum += data[i] - data[i - window];
        result[i] = sum;
    }

    return result;
}

/// Calculate rolling minimum
auto rolling_min(span<const double> data, size_t window) -> vector<double> {
    const size_t n = data.size();
    vector<double> result(n, std::numeric_limits<double>::quiet_NaN());

    if (n < window || window == 0)
        return result;

    std::deque<size_t> dq; // Indices of potential minimums

    for (size_t i = 0; i < n; ++i) {
        // Remove elements outside window
        while (!dq.empty() && dq.front() <= i - window) {
            dq.pop_front();
        }

        // Remove elements larger than current
        while (!dq.empty() && data[dq.back()] >= data[i]) {
            dq.pop_back();
        }

        dq.push_back(i);

        if (i >= window - 1) {
            result[i] = data[dq.front()];
        }
    }

    return result;
}

/// Calculate rolling maximum
auto rolling_max(span<const double> data, size_t window) -> vector<double> {
    const size_t n = data.size();
    vector<double> result(n, std::numeric_limits<double>::quiet_NaN());

    if (n < window || window == 0)
        return result;

    std::deque<size_t> dq; // Indices of potential maximums

    for (size_t i = 0; i < n; ++i) {
        while (!dq.empty() && dq.front() <= i - window) {
            dq.pop_front();
        }

        while (!dq.empty() && data[dq.back()] <= data[i]) {
            dq.pop_back();
        }

        dq.push_back(i);

        if (i >= window - 1) {
            result[i] = data[dq.front()];
        }
    }

    return result;
}

/// Calculate rolling z-score (standardized values)
auto rolling_zscore(span<const double> data, size_t window) -> vector<double> {
    const size_t n = data.size();
    vector<double> result(n, std::numeric_limits<double>::quiet_NaN());

    auto stats = rolling_stats(data, window);

    for (size_t i = window - 1; i < n; ++i) {
        if (stats.std_dev[i] > 1e-10) {
            result[i] = (data[i] - stats.mean[i]) / stats.std_dev[i];
        } else {
            result[i] = 0.0; // No variation
        }
    }

    return result;
}

/// Calculate rolling percentile rank (0-100)
auto rolling_percentile_rank(span<const double> data, size_t window) -> vector<double> {
    const size_t n = data.size();
    vector<double> result(n, std::numeric_limits<double>::quiet_NaN());

    if (n < window || window == 0)
        return result;

    for (size_t i = window - 1; i < n; ++i) {
        double current = data[i];
        size_t count_below = 0;

        for (size_t j = i + 1 - window; j < i; ++j) {
            if (data[j] < current) {
                ++count_below;
            }
        }

        result[i] = 100.0 * static_cast<double>(count_below) / static_cast<double>(window - 1);
    }

    return result;
}

/// Exponentially weighted moving average
auto ewma(span<const double> data, double span_param) -> vector<double> {
    const size_t n = data.size();
    vector<double> result(n);

    if (n == 0)
        return result;

    // Alpha from span: alpha = 2 / (span + 1)
    double alpha = 2.0 / (span_param + 1.0);

    result[0] = data[0];
    for (size_t i = 1; i < n; ++i) {
        result[i] = alpha * data[i] + (1.0 - alpha) * result[i - 1];
    }

    return result;
}

/// Exponentially weighted moving standard deviation
auto ewma_std(span<const double> data, double span_param) -> vector<double> {
    const size_t n = data.size();
    vector<double> result(n, std::numeric_limits<double>::quiet_NaN());

    if (n < 2)
        return result;

    double alpha = 2.0 / (span_param + 1.0);
    double ewma_val = data[0];
    double ewma_sq = data[0] * data[0];

    for (size_t i = 1; i < n; ++i) {
        ewma_val = alpha * data[i] + (1.0 - alpha) * ewma_val;
        ewma_sq = alpha * data[i] * data[i] + (1.0 - alpha) * ewma_sq;

        double var = ewma_sq - ewma_val * ewma_val;
        result[i] = var > 0 ? std::sqrt(var) : 0.0;
    }

    return result;
}

} // namespace finkit::stats
