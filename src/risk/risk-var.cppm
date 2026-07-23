/// @file risk-var.cppm
/// @brief Value-at-Risk: historical, parametric (variance-covariance), Monte Carlo
///
/// All three estimators return VaR and Expected Shortfall at a configurable
/// confidence level and horizon. Sign convention: VaR/ES are positive numbers
/// representing losses; a negative VaR means even the tail quantile is a gain.

module;

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <vector>

export module finkit.risk:var;

import finkit.stats;

namespace finkit::risk::detail {

using std::vector;

// Acklam's rational approximation coefficients (file-local, not exported)
constexpr double ACKLAM_A1 = -3.969683028665376e+01;
constexpr double ACKLAM_A2 = 2.209460984245205e+02;
constexpr double ACKLAM_A3 = -2.759285104469687e+02;
constexpr double ACKLAM_A4 = 1.383577518672690e+02;
constexpr double ACKLAM_A5 = -3.066479806614716e+01;
constexpr double ACKLAM_A6 = 2.506628277459239e+00;
constexpr double ACKLAM_B1 = -5.447609879822406e+01;
constexpr double ACKLAM_B2 = 1.615858368580409e+02;
constexpr double ACKLAM_B3 = -1.556989798598866e+02;
constexpr double ACKLAM_B4 = 6.680131188771972e+01;
constexpr double ACKLAM_B5 = -1.328068155288572e+01;
constexpr double ACKLAM_C1 = -7.784894002430293e-03;
constexpr double ACKLAM_C2 = -3.223964580411365e-01;
constexpr double ACKLAM_C3 = -2.400758277161838e+00;
constexpr double ACKLAM_C4 = -2.549732539343734e+00;
constexpr double ACKLAM_C5 = 4.374664141464968e+00;
constexpr double ACKLAM_C6 = 2.938163982698783e+00;
constexpr double ACKLAM_D1 = 7.784695709041462e-03;
constexpr double ACKLAM_D2 = 3.224671290700398e-01;
constexpr double ACKLAM_D3 = 2.445134137142996e+00;
constexpr double ACKLAM_D4 = 3.754408661907416e+00;

constexpr double SQRT_2PI = 2.5066282746310005024; // sqrt(2*pi), normal pdf normalizer

/// Empirical loss tail: returns {var, es}. Caller guarantees pnl.size() >= 2.
auto empirical_tail(vector<double> pnl, double confidence) -> std::pair<double, double> {
    const auto n = pnl.size();
    std::sort(pnl.begin(), pnl.end());
    // Tolerance guards fp double-rounding: e.g. (1 - 0.95) * 100 evaluates to
    // 5.000000000000004, whose ceil would wrongly bump k from 5 to 6 (a
    // systematically less conservative VaR at exact quantile boundaries).
    auto k = static_cast<size_t>(
        std::ceil((1.0 - confidence) * static_cast<double>(n) - 1e-9));
    if (k < 1) {
        k = 1;
    }
    if (k > n) {
        k = n;
    }
    double var = -pnl[k - 1];
    double tail_sum = 0.0;
    for (size_t i = 0; i < k; ++i) {
        tail_sum += pnl[i];
    }
    double es = -tail_sum / static_cast<double>(k);
    return {var, es};
}

} // namespace finkit::risk::detail

export namespace finkit::risk {

using std::span;
using std::string;
using std::vector;

using finkit::stats::CovarianceMatrix;

// ============================================================================
// Types
// ============================================================================

enum class VaRMethod { Historical, Parametric, MonteCarlo };

/// Configuration for a VaR calculation
struct VaRConfig {
    double confidence{0.99};       // Confidence level, in (0,1) exclusive
    double horizon_days{1.0};      // Horizon; result scaled by sqrt(horizon_days)
    size_t num_simulations{10000}; // Monte Carlo only (min 100)
    uint64_t seed{42};             // Monte Carlo only: std::mt19937_64 seed
};

/// Result of a VaR calculation
struct VaRResult {
    VaRMethod method{VaRMethod::Historical};
    double var{0.0};                // Positive = loss, in P&L/exposure units
    double expected_shortfall{0.0}; // Mean loss beyond VaR (positive = loss)
    double confidence{0.99};        // Echoed from config
    double horizon_days{1.0};       // Echoed from config
    size_t observations{0}; // n pnl obs (Historical), cov.observations (Parametric), sims (MC)
    bool success{false};
    string error_message;
};

// ============================================================================
// Normal distribution helpers
// ============================================================================

/// Inverse standard normal CDF (Acklam's rational approximation, |rel err| < 1.15e-9).
/// Returns quiet_NaN unless 0 < p < 1.
[[nodiscard]] auto normal_quantile(double p) -> double {
    if (!(p > 0.0 && p < 1.0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    using namespace finkit::risk::detail;
    constexpr double p_low = 0.02425;
    constexpr double p_high = 1.0 - p_low;
    if (p < p_low) {
        double q = std::sqrt(-2.0 * std::log(p));
        return (((((ACKLAM_C1 * q + ACKLAM_C2) * q + ACKLAM_C3) * q + ACKLAM_C4) * q +
                 ACKLAM_C5) *
                    q +
                ACKLAM_C6) /
               ((((ACKLAM_D1 * q + ACKLAM_D2) * q + ACKLAM_D3) * q + ACKLAM_D4) * q + 1.0);
    }
    if (p <= p_high) {
        double q = p - 0.5;
        double r = q * q;
        return (((((ACKLAM_A1 * r + ACKLAM_A2) * r + ACKLAM_A3) * r + ACKLAM_A4) * r +
                 ACKLAM_A5) *
                    r +
                ACKLAM_A6) *
               q /
               (((((ACKLAM_B1 * r + ACKLAM_B2) * r + ACKLAM_B3) * r + ACKLAM_B4) * r +
                 ACKLAM_B5) *
                    r +
                1.0);
    }
    double q = std::sqrt(-2.0 * std::log(1.0 - p));
    return -(((((ACKLAM_C1 * q + ACKLAM_C2) * q + ACKLAM_C3) * q + ACKLAM_C4) * q +
              ACKLAM_C5) *
                 q +
             ACKLAM_C6) /
           ((((ACKLAM_D1 * q + ACKLAM_D2) * q + ACKLAM_D3) * q + ACKLAM_D4) * q + 1.0);
}

/// Standard normal density: exp(-z*z/2) / sqrt(2*pi).
[[nodiscard]] auto normal_pdf(double z) -> double {
    return std::exp(-0.5 * z * z) / finkit::risk::detail::SQRT_2PI;
}

// ============================================================================
// VaR estimators
// ============================================================================

/// Historical VaR/ES: empirical tail of a per-period P&L vector.
/// @param pnl Per-period portfolio P&L (positive = gain), one entry per period (e.g. daily).
[[nodiscard]] auto historical_var(span<const double> pnl,
                                  const VaRConfig& config = {}) -> VaRResult {
    VaRResult result;
    result.method = VaRMethod::Historical;
    result.confidence = config.confidence;
    result.horizon_days = config.horizon_days;

    if (!(config.confidence > 0.0 && config.confidence < 1.0)) {
        result.error_message = "confidence must be in (0,1)";
        return result;
    }
    if (config.horizon_days <= 0.0) {
        result.error_message = "horizon_days must be > 0";
        return result;
    }
    if (pnl.size() < 2) {
        result.error_message = "need at least 2 P&L observations";
        return result;
    }

    auto [v, e] = detail::empirical_tail(vector<double>(pnl.begin(), pnl.end()), config.confidence);
    double scale = std::sqrt(config.horizon_days);
    result.var = v * scale;
    result.expected_shortfall = e * scale;
    result.observations = pnl.size();
    result.success = true;
    return result;
}

/// Parametric (variance-covariance) VaR/ES under a zero-mean normal assumption.
/// @param exposures Signed currency exposure per asset (e.g. quantity * price in base ccy),
///                  ordered to match cov.symbols.
/// @param cov       Per-period *return* covariance (from finkit::stats::calculate_covariance_matrix
///                  or calculate_ewma_covariance).
[[nodiscard]] auto parametric_var(span<const double> exposures, const CovarianceMatrix& cov,
                                  const VaRConfig& config = {}) -> VaRResult {
    VaRResult result;
    result.method = VaRMethod::Parametric;
    result.confidence = config.confidence;
    result.horizon_days = config.horizon_days;

    if (!(config.confidence > 0.0 && config.confidence < 1.0)) {
        result.error_message = "confidence must be in (0,1)";
        return result;
    }
    if (config.horizon_days <= 0.0) {
        result.error_message = "horizon_days must be > 0";
        return result;
    }
    const auto n = exposures.size();
    if (n == 0 || cov.size() != n) {
        result.error_message = "exposures/covariance size mismatch";
        return result;
    }

    Eigen::Map<const Eigen::VectorXd> w(exposures.data(), static_cast<Eigen::Index>(n));
    Eigen::MatrixXd s(static_cast<Eigen::Index>(n), static_cast<Eigen::Index>(n));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            s(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) = cov.matrix[i][j];
        }
    }

    double var_p = w.transpose() * s * w;
    if (var_p < 0.0) {
        var_p = 0.0;
    }
    double sigma = std::sqrt(var_p);
    double z = normal_quantile(config.confidence);
    double scale = std::sqrt(config.horizon_days);
    result.var = z * sigma * scale;
    result.expected_shortfall = sigma * normal_pdf(z) / (1.0 - config.confidence) * scale;
    result.observations = cov.observations;
    result.success = true;
    return result;
}

/// Monte Carlo VaR/ES: Cholesky-correlated standard-normal return draws,
/// linear (delta) portfolio revaluation pnl = exposures . r.
///
/// Determinism: same seed + same standard library => bit-identical results.
/// std::normal_distribution's algorithm is implementation-defined, so
/// cross-stdlib reproducibility is not guaranteed.
[[nodiscard]] auto monte_carlo_var(span<const double> exposures, const CovarianceMatrix& cov,
                                   const VaRConfig& config = {}) -> VaRResult {
    VaRResult result;
    result.method = VaRMethod::MonteCarlo;
    result.confidence = config.confidence;
    result.horizon_days = config.horizon_days;

    if (!(config.confidence > 0.0 && config.confidence < 1.0)) {
        result.error_message = "confidence must be in (0,1)";
        return result;
    }
    if (config.horizon_days <= 0.0) {
        result.error_message = "horizon_days must be > 0";
        return result;
    }
    const auto n = exposures.size();
    if (n == 0 || cov.size() != n) {
        result.error_message = "exposures/covariance size mismatch";
        return result;
    }
    if (config.num_simulations < 100) {
        result.error_message = "num_simulations must be >= 100";
        return result;
    }

    Eigen::MatrixXd s(static_cast<Eigen::Index>(n), static_cast<Eigen::Index>(n));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            s(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) = cov.matrix[i][j];
        }
    }

    Eigen::LLT<Eigen::MatrixXd> llt(s);
    if (llt.info() != Eigen::Success) {
        result.error_message = "covariance matrix not positive definite";
        return result;
    }
    Eigen::MatrixXd l = llt.matrixL();

    std::mt19937_64 rng{config.seed};
    std::normal_distribution<double> nd{0.0, 1.0};
    Eigen::Map<const Eigen::VectorXd> w(exposures.data(), static_cast<Eigen::Index>(n));

    vector<double> pnl;
    pnl.reserve(config.num_simulations);
    Eigen::VectorXd z(static_cast<Eigen::Index>(n));
    for (size_t sidx = 0; sidx < config.num_simulations; ++sidx) {
        for (size_t i = 0; i < n; ++i) {
            z(static_cast<Eigen::Index>(i)) = nd(rng);
        }
        Eigen::VectorXd r = l * z;
        pnl.push_back(w.dot(r));
    }

    auto [v, e] = detail::empirical_tail(std::move(pnl), config.confidence);
    double scale = std::sqrt(config.horizon_days);
    result.var = v * scale;
    result.expected_shortfall = e * scale;
    result.observations = config.num_simulations;
    result.success = true;
    return result;
}

} // namespace finkit::risk
