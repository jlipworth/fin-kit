/// @file stats-covariance.cppm
/// @brief Covariance and correlation matrix calculations
///
/// Provides covariance matrix estimation, correlation analysis, and
/// related linear algebra operations for portfolio risk calculations.
/// Uses Eigen for numerical stability and performance.

module;

#include <Eigen/Dense>
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
    if (x.size() != y.size() || x.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const auto n = static_cast<Eigen::Index>(x.size());

    // Map to Eigen vectors (no copy)
    Eigen::Map<const Eigen::VectorXd> vx(x.data(), n);
    Eigen::Map<const Eigen::VectorXd> vy(y.data(), n);

    // Center the data
    Eigen::VectorXd cx = vx.array() - vx.mean();
    Eigen::VectorXd cy = vy.array() - vy.mean();

    // Sample covariance: dot(cx, cy) / (n - 1)
    return cx.dot(cy) / static_cast<double>(n - 1);
}

/// Calculate Pearson correlation between two series
auto correlation(span<const double> x, span<const double> y) -> double {
    if (x.size() != y.size() || x.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const auto n = static_cast<Eigen::Index>(x.size());

    // Map to Eigen vectors (no copy)
    Eigen::Map<const Eigen::VectorXd> vx(x.data(), n);
    Eigen::Map<const Eigen::VectorXd> vy(y.data(), n);

    // Center the data
    Eigen::VectorXd cx = vx.array() - vx.mean();
    Eigen::VectorXd cy = vy.array() - vy.mean();

    // Correlation: dot(cx, cy) / (norm(cx) * norm(cy))
    double denom = cx.norm() * cy.norm();
    return denom > 1e-10 ? cx.dot(cy) / denom : 0.0;
}

/// Calculate covariance matrix from return series
/// @param returns Matrix of returns: returns[symbol_idx][time_idx]
/// @param symbols Symbol names for labeling
/// @return CovarianceMatrix with sample covariances
auto calculate_covariance_matrix(const vector<vector<double>>& returns,
                                 const vector<string>& symbols) -> CovarianceMatrix {
    const auto n_assets = static_cast<Eigen::Index>(returns.size());
    if (n_assets == 0 || returns[0].empty()) {
        return CovarianceMatrix{};
    }

    const auto n_obs = static_cast<Eigen::Index>(returns[0].size());

    // Need at least 2 observations for sample covariance (divides by n_obs - 1)
    if (n_obs <= 1) {
        return CovarianceMatrix{};
    }

    // Build Eigen matrix from input (assets x observations)
    Eigen::MatrixXd R(n_assets, n_obs);
    for (Eigen::Index i = 0; i < n_assets; ++i) {
        for (Eigen::Index t = 0; t < n_obs; ++t) {
            R(i, t) = returns[static_cast<size_t>(i)][static_cast<size_t>(t)];
        }
    }

    // Center: subtract row means
    Eigen::VectorXd means = R.rowwise().mean();
    Eigen::MatrixXd centered = R.colwise() - means;

    // Covariance: (centered * centered') / (n - 1)
    Eigen::MatrixXd cov = (centered * centered.transpose()) / static_cast<double>(n_obs - 1);

    // Convert back to vector<vector<double>>
    vector<vector<double>> cov_matrix(static_cast<size_t>(n_assets),
                                      vector<double>(static_cast<size_t>(n_assets)));
    for (Eigen::Index i = 0; i < n_assets; ++i) {
        for (Eigen::Index j = 0; j < n_assets; ++j) {
            cov_matrix[static_cast<size_t>(i)][static_cast<size_t>(j)] = cov(i, j);
        }
    }

    return CovarianceMatrix{.matrix = std::move(cov_matrix),
                            .symbols = symbols,
                            .observations = static_cast<size_t>(n_obs),
                            .lookback_days = std::nullopt};
}

/// Convert covariance matrix to correlation matrix
auto to_correlation_matrix(const CovarianceMatrix& cov) -> CorrelationMatrix {
    const auto n = static_cast<Eigen::Index>(cov.size());
    if (n == 0) {
        return CorrelationMatrix{};
    }

    // Build Eigen matrix from input
    Eigen::MatrixXd S(n, n);
    for (Eigen::Index i = 0; i < n; ++i) {
        for (Eigen::Index j = 0; j < n; ++j) {
            S(i, j) = cov.get(static_cast<size_t>(i), static_cast<size_t>(j));
        }
    }

    // Get standard deviations from diagonal
    Eigen::VectorXd std_devs = S.diagonal().array().sqrt();

    // Correlation: C_ij = S_ij / (sigma_i * sigma_j)
    Eigen::MatrixXd C(n, n);
    for (Eigen::Index i = 0; i < n; ++i) {
        for (Eigen::Index j = 0; j < n; ++j) {
            if (std_devs(i) > 1e-10 && std_devs(j) > 1e-10) {
                C(i, j) = S(i, j) / (std_devs(i) * std_devs(j));
            } else {
                C(i, j) = (i == j) ? 1.0 : 0.0;
            }
        }
    }

    // Convert back to vector<vector<double>>
    vector<vector<double>> corr_matrix(static_cast<size_t>(n),
                                       vector<double>(static_cast<size_t>(n)));
    for (Eigen::Index i = 0; i < n; ++i) {
        for (Eigen::Index j = 0; j < n; ++j) {
            corr_matrix[static_cast<size_t>(i)][static_cast<size_t>(j)] = C(i, j);
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
    const auto n_assets = static_cast<Eigen::Index>(returns.size());
    if (n_assets == 0 || returns[0].empty()) {
        return CovarianceMatrix{};
    }

    const auto n_obs = static_cast<Eigen::Index>(returns[0].size());

    // Need at least 2 observations for bias correction (divides by 1 - sum(w^2))
    if (n_obs < 2) {
        return CovarianceMatrix{};
    }

    // Decay factor from halflife
    double lambda = std::pow(0.5, 1.0 / halflife);

    // Calculate weights (most recent has highest weight)
    Eigen::VectorXd weights(n_obs);
    for (Eigen::Index t = 0; t < n_obs; ++t) {
        weights(t) = std::pow(lambda, static_cast<double>(n_obs - 1 - t));
    }
    weights /= weights.sum(); // Normalize

    // Build Eigen matrix from input (assets x observations)
    Eigen::MatrixXd R(n_assets, n_obs);
    for (Eigen::Index i = 0; i < n_assets; ++i) {
        for (Eigen::Index t = 0; t < n_obs; ++t) {
            R(i, t) = returns[static_cast<size_t>(i)][static_cast<size_t>(t)];
        }
    }

    // Weighted means: sum(w_t * r_t) for each asset
    Eigen::VectorXd means = R * weights;

    // Center the data
    Eigen::MatrixXd centered = R.colwise() - means;

    // Weight the centered data: each column scaled by sqrt(weight)
    Eigen::MatrixXd weighted_centered =
        centered.array().rowwise() * weights.transpose().array().sqrt();

    // Weighted covariance: (weighted_centered * weighted_centered')
    Eigen::MatrixXd cov = weighted_centered * weighted_centered.transpose();

    // Bias correction for weighted sample: 1 / (1 - sum(w^2))
    double sum_sq_weights = weights.squaredNorm();
    double correction = 1.0 / (1.0 - sum_sq_weights);
    cov *= correction;

    // Convert back to vector<vector<double>>
    vector<vector<double>> cov_matrix(static_cast<size_t>(n_assets),
                                      vector<double>(static_cast<size_t>(n_assets)));
    for (Eigen::Index i = 0; i < n_assets; ++i) {
        for (Eigen::Index j = 0; j < n_assets; ++j) {
            cov_matrix[static_cast<size_t>(i)][static_cast<size_t>(j)] = cov(i, j);
        }
    }

    return CovarianceMatrix{.matrix = std::move(cov_matrix),
                            .symbols = symbols,
                            .observations = static_cast<size_t>(n_obs),
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
    const auto n = static_cast<Eigen::Index>(sample_cov.size());
    if (n == 0)
        return sample_cov;

    // Build Eigen matrix from input
    Eigen::MatrixXd S(n, n);
    for (Eigen::Index i = 0; i < n; ++i) {
        for (Eigen::Index j = 0; j < n; ++j) {
            S(i, j) = sample_cov.get(static_cast<size_t>(i), static_cast<size_t>(j));
        }
    }

    // Target: scaled identity matrix with average variance
    double avg_var = S.diagonal().mean();
    Eigen::MatrixXd target = avg_var * Eigen::MatrixXd::Identity(n, n);

    // Shrinkage: alpha * target + (1 - alpha) * sample
    Eigen::MatrixXd shrunk = shrinkage_intensity * target + (1.0 - shrinkage_intensity) * S;

    // Convert back to vector<vector<double>>
    vector<vector<double>> shrunk_matrix(static_cast<size_t>(n),
                                         vector<double>(static_cast<size_t>(n)));
    for (Eigen::Index i = 0; i < n; ++i) {
        for (Eigen::Index j = 0; j < n; ++j) {
            shrunk_matrix[static_cast<size_t>(i)][static_cast<size_t>(j)] = shrunk(i, j);
        }
    }

    return CovarianceMatrix{.matrix = std::move(shrunk_matrix),
                            .symbols = sample_cov.symbols,
                            .observations = sample_cov.observations,
                            .lookback_days = sample_cov.lookback_days};
}

} // namespace finkit::stats
