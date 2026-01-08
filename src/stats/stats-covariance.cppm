/// @file stats-covariance.cppm
/// @brief Covariance and correlation matrix calculations
///
/// Provides covariance matrix estimation, correlation analysis, and
/// related linear algebra operations for portfolio risk calculations.

module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module finkit.stats:covariance;

export namespace finkit::stats {

using std::optional;
using std::span;
using std::string;
using std::vector;

/// Covariance matrix result with metadata
struct CovarianceMatrix {
    vector<vector<double>> matrix;
    vector<string> symbols;
    size_t observations{0};
    optional<size_t> lookback_days;

    [[nodiscard]] auto size() const -> size_t { return matrix.size(); }

    [[nodiscard]] auto get(size_t i, size_t j) const -> double { return matrix[i][j]; }

    [[nodiscard]] auto variance(size_t i) const -> double { return matrix[i][i]; }

    [[nodiscard]] auto std_dev(size_t i) const -> double { return std::sqrt(matrix[i][i]); }
};

/// Correlation matrix result
struct CorrelationMatrix {
    vector<vector<double>> matrix;
    vector<string> symbols;
    size_t observations{0};

    [[nodiscard]] auto size() const -> size_t { return matrix.size(); }

    [[nodiscard]] auto get(size_t i, size_t j) const -> double { return matrix[i][j]; }
};

/// Calculate sample covariance between two series
auto covariance(span<const double> x, span<const double> y) -> double {
    if (x.size() != y.size() || x.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const size_t n = x.size();

    double mean_x = 0.0, mean_y = 0.0;
    for (size_t i = 0; i < n; ++i) {
        mean_x += x[i];
        mean_y += y[i];
    }
    mean_x /= static_cast<double>(n);
    mean_y /= static_cast<double>(n);

    double cov = 0.0;
    for (size_t i = 0; i < n; ++i) {
        cov += (x[i] - mean_x) * (y[i] - mean_y);
    }

    return cov / static_cast<double>(n - 1); // Sample covariance
}

/// Calculate Pearson correlation between two series
auto correlation(span<const double> x, span<const double> y) -> double {
    if (x.size() != y.size() || x.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const size_t n = x.size();

    double mean_x = 0.0, mean_y = 0.0;
    for (size_t i = 0; i < n; ++i) {
        mean_x += x[i];
        mean_y += y[i];
    }
    mean_x /= static_cast<double>(n);
    mean_y /= static_cast<double>(n);

    double cov = 0.0, var_x = 0.0, var_y = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double dx = x[i] - mean_x;
        double dy = y[i] - mean_y;
        cov += dx * dy;
        var_x += dx * dx;
        var_y += dy * dy;
    }

    double denom = std::sqrt(var_x * var_y);
    return denom > 1e-10 ? cov / denom : 0.0;
}

/// Calculate covariance matrix from return series
/// @param returns Matrix of returns: returns[symbol_idx][time_idx]
/// @param symbols Symbol names for labeling
/// @return CovarianceMatrix with sample covariances
auto calculate_covariance_matrix(const vector<vector<double>>& returns,
                                 const vector<string>& symbols) -> CovarianceMatrix {
    const size_t n_assets = returns.size();
    if (n_assets == 0 || returns[0].empty()) {
        return CovarianceMatrix{};
    }

    const size_t n_obs = returns[0].size();

    // Compute means
    vector<double> means(n_assets, 0.0);
    for (size_t i = 0; i < n_assets; ++i) {
        for (size_t t = 0; t < n_obs; ++t) {
            means[i] += returns[i][t];
        }
        means[i] /= static_cast<double>(n_obs);
    }

    // Compute covariance matrix
    vector<vector<double>> cov_matrix(n_assets, vector<double>(n_assets, 0.0));

    for (size_t i = 0; i < n_assets; ++i) {
        for (size_t j = i; j < n_assets; ++j) {
            double cov = 0.0;
            for (size_t t = 0; t < n_obs; ++t) {
                cov += (returns[i][t] - means[i]) * (returns[j][t] - means[j]);
            }
            cov /= static_cast<double>(n_obs - 1);

            cov_matrix[i][j] = cov;
            cov_matrix[j][i] = cov; // Symmetric
        }
    }

    return CovarianceMatrix{.matrix = std::move(cov_matrix),
                            .symbols = symbols,
                            .observations = n_obs,
                            .lookback_days = std::nullopt};
}

/// Convert covariance matrix to correlation matrix
auto to_correlation_matrix(const CovarianceMatrix& cov) -> CorrelationMatrix {
    const size_t n = cov.size();
    vector<vector<double>> corr_matrix(n, vector<double>(n, 0.0));

    vector<double> std_devs(n);
    for (size_t i = 0; i < n; ++i) {
        std_devs[i] = cov.std_dev(i);
    }

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (std_devs[i] > 1e-10 && std_devs[j] > 1e-10) {
                corr_matrix[i][j] = cov.get(i, j) / (std_devs[i] * std_devs[j]);
            } else {
                corr_matrix[i][j] = (i == j) ? 1.0 : 0.0;
            }
        }
    }

    return CorrelationMatrix{
        .matrix = std::move(corr_matrix), .symbols = cov.symbols, .observations = cov.observations};
}

/// Calculate exponentially weighted covariance matrix
/// @param returns Matrix of returns: returns[symbol_idx][time_idx]
/// @param symbols Symbol names
/// @param halflife Halflife in periods for exponential weighting
auto calculate_ewma_covariance(const vector<vector<double>>& returns, const vector<string>& symbols,
                               double halflife) -> CovarianceMatrix {
    const size_t n_assets = returns.size();
    if (n_assets == 0 || returns[0].empty()) {
        return CovarianceMatrix{};
    }

    const size_t n_obs = returns[0].size();

    // Decay factor from halflife
    double lambda = std::pow(0.5, 1.0 / halflife);

    // Calculate weights (most recent has highest weight)
    vector<double> weights(n_obs);
    double weight_sum = 0.0;
    for (size_t t = 0; t < n_obs; ++t) {
        weights[t] = std::pow(lambda, static_cast<double>(n_obs - 1 - t));
        weight_sum += weights[t];
    }
    for (auto& w : weights) {
        w /= weight_sum;
    }

    // Compute weighted means
    vector<double> means(n_assets, 0.0);
    for (size_t i = 0; i < n_assets; ++i) {
        for (size_t t = 0; t < n_obs; ++t) {
            means[i] += weights[t] * returns[i][t];
        }
    }

    // Compute weighted covariance matrix
    vector<vector<double>> cov_matrix(n_assets, vector<double>(n_assets, 0.0));

    for (size_t i = 0; i < n_assets; ++i) {
        for (size_t j = i; j < n_assets; ++j) {
            double cov = 0.0;
            for (size_t t = 0; t < n_obs; ++t) {
                cov += weights[t] * (returns[i][t] - means[i]) * (returns[j][t] - means[j]);
            }
            // Bias correction for weighted sample
            double sum_sq_weights =
                std::inner_product(weights.begin(), weights.end(), weights.begin(), 0.0);
            double correction = 1.0 / (1.0 - sum_sq_weights);
            cov *= correction;

            cov_matrix[i][j] = cov;
            cov_matrix[j][i] = cov;
        }
    }

    return CovarianceMatrix{.matrix = std::move(cov_matrix),
                            .symbols = symbols,
                            .observations = n_obs,
                            .lookback_days = std::nullopt};
}

/// Calculate rolling correlation between two series
auto rolling_correlation(span<const double> x, span<const double> y,
                         size_t window) -> vector<double> {
    if (x.size() != y.size()) {
        return {};
    }

    const size_t n = x.size();
    vector<double> result(n, std::numeric_limits<double>::quiet_NaN());

    if (n < window || window < 2)
        return result;

    for (size_t i = window - 1; i < n; ++i) {
        span<const double> x_window(x.data() + i + 1 - window, window);
        span<const double> y_window(y.data() + i + 1 - window, window);
        result[i] = correlation(x_window, y_window);
    }

    return result;
}

/// Calculate rolling beta (regression slope) of y on x
auto rolling_beta(span<const double> y, span<const double> x, size_t window) -> vector<double> {
    if (x.size() != y.size()) {
        return {};
    }

    const size_t n = x.size();
    vector<double> result(n, std::numeric_limits<double>::quiet_NaN());

    if (n < window || window < 2)
        return result;

    for (size_t i = window - 1; i < n; ++i) {
        span<const double> x_window(x.data() + i + 1 - window, window);
        span<const double> y_window(y.data() + i + 1 - window, window);

        double cov_xy = covariance(x_window, y_window);
        double var_x = covariance(x_window, x_window);

        result[i] = var_x > 1e-10 ? cov_xy / var_x : 0.0;
    }

    return result;
}

/// Shrinkage estimator for covariance matrix (Ledoit-Wolf style)
/// Shrinks toward identity matrix scaled by average variance
auto shrink_covariance(const CovarianceMatrix& sample_cov,
                       double shrinkage_intensity) -> CovarianceMatrix {
    const size_t n = sample_cov.size();
    if (n == 0)
        return sample_cov;

    // Calculate average variance (diagonal mean)
    double avg_var = 0.0;
    for (size_t i = 0; i < n; ++i) {
        avg_var += sample_cov.variance(i);
    }
    avg_var /= static_cast<double>(n);

    // Target: scaled identity matrix
    vector<vector<double>> shrunk(n, vector<double>(n, 0.0));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            double target = (i == j) ? avg_var : 0.0;
            shrunk[i][j] =
                shrinkage_intensity * target + (1.0 - shrinkage_intensity) * sample_cov.get(i, j);
        }
    }

    return CovarianceMatrix{.matrix = std::move(shrunk),
                            .symbols = sample_cov.symbols,
                            .observations = sample_cov.observations,
                            .lookback_days = sample_cov.lookback_days};
}

} // namespace finkit::stats
