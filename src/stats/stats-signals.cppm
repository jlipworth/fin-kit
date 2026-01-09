/// @file stats-signals.cppm
/// @brief Signal analysis and quality metrics
///
/// Provides tools for analyzing trading signals, including signal quality
/// metrics, hit rates, and information coefficients.

module;

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <span>
#include <string>
#include <vector>

export module finkit.stats:signals;

export namespace finkit::stats {

using std::span;
using std::string;
using std::vector;

/// Signal quality metrics
struct SignalMetrics {
    double hit_rate{0.0};            // % of correct direction predictions
    double information_coef{0.0};    // IC: correlation between signal and returns
    double information_ratio{0.0};   // IC / std(IC)
    double avg_signal_strength{0.0}; // Mean absolute signal value
    double signal_autocorr{0.0};     // First-order autocorrelation
    size_t observations{0};
    size_t correct_predictions{0};
    size_t total_predictions{0};
};

/// Signal decay analysis result
struct SignalDecay {
    vector<double> ic_by_horizon;     // IC at each forward horizon
    vector<double> cumulative_return; // Cumulative return by horizon
    vector<int> horizons;             // Forward periods (1, 2, 5, 10, ...)
};

/// Stationarity test result
struct StationarityResult {
    bool is_stationary{false};
    double adf_statistic{0.0}; // Augmented Dickey-Fuller statistic
    double critical_value_5pct{0.0};
    string interpretation;
};

namespace detail {

/// Helper: calculate ranks for Spearman correlation
inline auto calculate_ranks(span<const double> data) -> vector<size_t> {
    const size_t n = data.size();

    // Create indices sorted by value
    vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(),
              [&data](size_t a, size_t b) { return data[a] < data[b]; });

    // Assign ranks
    vector<size_t> ranks(n);
    for (size_t i = 0; i < n; ++i) {
        ranks[indices[i]] = i + 1;
    }

    return ranks;
}

} // namespace detail

/// Calculate Information Coefficient (rank correlation) using Eigen
auto calculate_ic(span<const double> signals, span<const double> returns) -> double {
    if (signals.size() != returns.size() || signals.size() < 2) {
        return 0.0;
    }

    const auto n = static_cast<Eigen::Index>(signals.size());

    // Spearman rank correlation
    vector<size_t> signal_ranks = detail::calculate_ranks(signals);
    vector<size_t> return_ranks = detail::calculate_ranks(returns);

    // Convert ranks to Eigen vectors
    Eigen::VectorXd rs(n), rr(n);
    for (Eigen::Index i = 0; i < n; ++i) {
        rs(i) = static_cast<double>(signal_ranks[static_cast<size_t>(i)]);
        rr(i) = static_cast<double>(return_ranks[static_cast<size_t>(i)]);
    }

    // Center the rank vectors
    Eigen::VectorXd cs = rs.array() - rs.mean();
    Eigen::VectorXd cr = rr.array() - rr.mean();

    // Pearson correlation of ranks: dot(cs, cr) / (norm(cs) * norm(cr))
    double denom = cs.norm() * cr.norm();
    return denom > 1e-10 ? cs.dot(cr) / denom : 0.0;
}

/// Calculate autocorrelation at specified lag using Eigen
auto calculate_autocorrelation(span<const double> data, size_t lag) -> double {
    if (data.size() <= lag) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const auto n_total = static_cast<Eigen::Index>(data.size());
    const auto n_pairs = static_cast<Eigen::Index>(data.size() - lag);

    // Map data to Eigen vector
    Eigen::Map<const Eigen::VectorXd> x(data.data(), n_total);

    double mean = x.mean();

    // Center the data
    Eigen::VectorXd centered = x.array() - mean;

    // Covariance: sum of (x_t - mean) * (x_{t+lag} - mean) for t = 0 to n-lag-1
    double cov = centered.head(n_pairs).dot(centered.tail(n_pairs));

    // Variance: sum of (x_t - mean)^2 for all t
    double var = centered.squaredNorm();

    return var > 1e-10 ? cov / var : 0.0;
}

/// Analyze signal quality against subsequent returns
/// @param signals Signal values (positive = long, negative = short)
/// @param returns Forward returns (aligned with signals)
/// @return SignalMetrics with quality statistics
auto analyze_signal(span<const double> signals, span<const double> returns) -> SignalMetrics {
    if (signals.size() != returns.size() || signals.empty()) {
        return SignalMetrics{};
    }

    const size_t n = signals.size();

    // Hit rate: signal direction matches return direction
    size_t correct = 0;
    size_t total = 0;
    double sum_abs_signal = 0.0;

    for (size_t i = 0; i < n; ++i) {
        if (std::abs(signals[i]) > 1e-10) { // Non-zero signal
            ++total;
            if ((signals[i] > 0 && returns[i] > 0) || (signals[i] < 0 && returns[i] < 0)) {
                ++correct;
            }
            sum_abs_signal += std::abs(signals[i]);
        }
    }

    double hit_rate = total > 0 ? static_cast<double>(correct) / static_cast<double>(total) : 0.0;

    // Information Coefficient (Spearman rank correlation)
    double ic = calculate_ic(signals, returns);

    // Signal autocorrelation
    double autocorr = calculate_autocorrelation(signals, 1);

    // Information Ratio (IC / std(rolling IC))
    // Simplified: use IC / estimated IC volatility
    double ir = std::abs(ic) / 0.05; // Rough estimate assuming IC std ~ 0.05

    return SignalMetrics{.hit_rate = hit_rate,
                         .information_coef = ic,
                         .information_ratio = ir,
                         .avg_signal_strength =
                             total > 0 ? sum_abs_signal / static_cast<double>(total) : 0.0,
                         .signal_autocorr = autocorr,
                         .observations = n,
                         .correct_predictions = correct,
                         .total_predictions = total};
}

/// Calculate rolling IC with specified window
auto rolling_ic(span<const double> signals, span<const double> returns,
                size_t window) -> vector<double> {
    if (signals.size() != returns.size()) {
        return {};
    }

    const size_t n = signals.size();
    vector<double> result(n, std::numeric_limits<double>::quiet_NaN());

    if (n < window)
        return result;

    for (size_t i = window - 1; i < n; ++i) {
        span<const double> sig_window(signals.data() + i + 1 - window, window);
        span<const double> ret_window(returns.data() + i + 1 - window, window);
        result[i] = calculate_ic(sig_window, ret_window);
    }

    return result;
}

/// Analyze signal decay over multiple horizons
/// @param signals Signal values
/// @param returns_matrix Returns at each horizon: returns_matrix[horizon_idx][time_idx]
/// @param horizons Forward periods to analyze (e.g., {1, 2, 5, 10, 21})
auto analyze_signal_decay(span<const double> signals, const vector<vector<double>>& returns_matrix,
                          span<const int> horizons) -> SignalDecay {
    SignalDecay result;
    result.horizons.assign(horizons.begin(), horizons.end());

    for (size_t h = 0; h < horizons.size(); ++h) {
        if (h < returns_matrix.size()) {
            double ic = calculate_ic(signals, span<const double>(returns_matrix[h]));
            result.ic_by_horizon.push_back(ic);

            // Calculate signal-weighted cumulative return
            double cum_ret = 0.0;
            const auto& rets = returns_matrix[h];
            for (size_t i = 0; i < signals.size() && i < rets.size(); ++i) {
                cum_ret += signals[i] * rets[i];
            }
            result.cumulative_return.push_back(cum_ret);
        }
    }

    return result;
}

/// Calculate partial autocorrelation function (PACF)
auto calculate_pacf(span<const double> data, size_t max_lag) -> vector<double> {
    vector<double> pacf;
    pacf.reserve(max_lag + 1);

    // Lag 0 is always 1
    pacf.push_back(1.0);

    // Calculate ACF first
    vector<double> acf;
    for (size_t k = 0; k <= max_lag; ++k) {
        acf.push_back(calculate_autocorrelation(data, k));
    }

    // Durbin-Levinson recursion for PACF
    vector<double> phi(max_lag + 1, 0.0);

    for (size_t k = 1; k <= max_lag; ++k) {
        // Calculate phi[k][k]
        double num = acf[k];
        for (size_t j = 1; j < k; ++j) {
            num -= phi[j] * acf[k - j];
        }

        double denom = 1.0;
        for (size_t j = 1; j < k; ++j) {
            denom -= phi[j] * acf[j];
        }

        double phi_kk = denom > 1e-10 ? num / denom : 0.0;
        pacf.push_back(phi_kk);

        // Update phi coefficients
        vector<double> phi_new(max_lag + 1, 0.0);
        for (size_t j = 1; j < k; ++j) {
            phi_new[j] = phi[j] - phi_kk * phi[k - j];
        }
        phi_new[k] = phi_kk;
        phi = std::move(phi_new);
    }

    return pacf;
}

/// Test for stationarity using simple heuristics (Eigen-based)
/// Returns p-value estimate (< 0.05 suggests stationarity)
auto test_stationarity(span<const double> data) -> StationarityResult {
    // Simplified ADF test approximation
    // Full implementation would require regression and lag selection

    if (data.size() < 20) {
        return StationarityResult{.is_stationary = false,
                                  .adf_statistic = 0.0,
                                  .critical_value_5pct = -2.86,
                                  .interpretation = "Insufficient data for stationarity test"};
    }

    const auto n = static_cast<Eigen::Index>(data.size() - 1);

    // Map data and calculate first differences
    Eigen::Map<const Eigen::VectorXd> y_full(data.data(), static_cast<Eigen::Index>(data.size()));

    // y_{t-1} for t = 1 to n (lagged level)
    Eigen::VectorXd y_lag = y_full.head(n);

    // diff_t = y_t - y_{t-1}
    Eigen::VectorXd diff = y_full.tail(n) - y_lag;

    // Simple regression: diff_t = alpha + beta * y_{t-1} + error
    // Under null (unit root), beta = 0
    double mean_y = y_lag.mean();
    double mean_diff = diff.mean();

    // Center both vectors
    Eigen::VectorXd y_centered = y_lag.array() - mean_y;
    Eigen::VectorXd diff_centered = diff.array() - mean_diff;

    // Beta = cov(y_lag, diff) / var(y_lag)
    double cov = y_centered.dot(diff_centered);
    double var_y = y_centered.squaredNorm();

    double beta = var_y > 1e-10 ? cov / var_y : 0.0;

    // Estimate standard error
    double alpha = mean_diff - beta * mean_y;
    Eigen::VectorXd residuals = diff - (alpha + beta * y_lag.array()).matrix();
    double sse = residuals.squaredNorm();
    double mse = sse / static_cast<double>(n - 2);
    double se_beta = std::sqrt(mse / var_y);

    double adf_stat = se_beta > 1e-10 ? beta / se_beta : 0.0;

    // Critical values (approximate for n=100)
    double cv_5pct = -2.86;

    bool stationary = adf_stat < cv_5pct;

    return StationarityResult{
        .is_stationary = stationary,
        .adf_statistic = adf_stat,
        .critical_value_5pct = cv_5pct,
        .interpretation = stationary ? "Series appears stationary (reject unit root)"
                                     : "Series appears non-stationary (cannot reject unit root)"};
}

} // namespace finkit::stats
