/// @file stats-returns.cppm
/// @brief Return-based performance metrics
///
/// Provides Sharpe ratio, Sortino ratio, Calmar ratio, and other
/// common return statistics. All calculations use Eigen for numerical stability.

module;

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

export module finkit.stats:returns;

export namespace finkit::stats {

using std::span;
using std::vector;

/// Performance metrics result
struct ReturnMetrics {
    double total_return{0.0};      // Cumulative return
    double annualized_return{0.0}; // CAGR
    double sharpe_ratio{0.0};      // Risk-adjusted return (vs risk-free)
    double sortino_ratio{0.0};     // Downside risk-adjusted return
    double calmar_ratio{0.0};      // Return / max drawdown
    double volatility{0.0};        // Annualized std dev
    double downside_vol{0.0};      // Annualized downside std dev
    double max_drawdown{0.0};      // Maximum peak-to-trough decline
    double skewness{0.0};          // Return distribution skewness
    double kurtosis{0.0};          // Return distribution kurtosis (excess)
    size_t observations{0};
};

/// Calculate Sharpe ratio from returns
/// @param returns Period returns (e.g., daily)
/// @param risk_free_rate Risk-free rate per period (default 0)
/// @param periods_per_year Number of periods per year (252 for daily)
/// @return Annualized Sharpe ratio
auto sharpe_ratio(span<const double> returns, double risk_free_rate = 0.0,
                  double periods_per_year = 252.0) -> double {
    if (returns.size() < 2) {
        return 0.0;
    }

    const auto n = static_cast<Eigen::Index>(returns.size());
    Eigen::Map<const Eigen::VectorXd> r(returns.data(), n);

    // Excess returns
    Eigen::VectorXd excess = r.array() - risk_free_rate;

    double mean_excess = excess.mean();
    Eigen::VectorXd centered = excess.array() - mean_excess;
    // Sample std dev (n-1) for consistency with annualized_volatility
    double std_dev = std::sqrt(centered.squaredNorm() / static_cast<double>(n - 1));

    if (std_dev < 1e-10) {
        return 0.0;
    }

    return (mean_excess / std_dev) * std::sqrt(periods_per_year);
}

/// Calculate Sortino ratio from returns (downside deviation only)
/// @param returns Period returns
/// @param target_return Minimum acceptable return per period (default 0)
/// @param periods_per_year Number of periods per year
/// @return Annualized Sortino ratio
auto sortino_ratio(span<const double> returns, double target_return = 0.0,
                   double periods_per_year = 252.0) -> double {
    if (returns.size() < 2) {
        return 0.0;
    }

    const auto n = static_cast<Eigen::Index>(returns.size());
    Eigen::Map<const Eigen::VectorXd> r(returns.data(), n);

    double mean_return = r.mean();

    // Downside deviation: std dev of returns below target
    double sum_sq_downside = 0.0;
    int count_downside = 0;
    for (Eigen::Index i = 0; i < n; ++i) {
        if (r(i) < target_return) {
            double diff = r(i) - target_return;
            sum_sq_downside += diff * diff;
            count_downside++;
        }
    }

    if (count_downside < 2) {
        return 0.0;
    }

    double downside_std = std::sqrt(sum_sq_downside / static_cast<double>(n));

    if (downside_std < 1e-10) {
        return 0.0;
    }

    return ((mean_return - target_return) / downside_std) * std::sqrt(periods_per_year);
}

/// Calculate maximum drawdown from returns
/// @param returns Period returns
/// @return Maximum drawdown as positive decimal (e.g., 0.20 = 20%)
auto max_drawdown(span<const double> returns) -> double {
    if (returns.empty()) {
        return 0.0;
    }

    double peak = 1.0;
    double current = 1.0;
    double max_dd = 0.0;

    for (const double ret : returns) {
        current *= (1.0 + ret);
        peak = std::max(peak, current);
        double dd = (peak - current) / peak;
        max_dd = std::max(max_dd, dd);
    }

    return max_dd;
}

/// Calculate Calmar ratio (annualized return / max drawdown)
/// @param returns Period returns
/// @param periods_per_year Number of periods per year
/// @return Calmar ratio
auto calmar_ratio(span<const double> returns, double periods_per_year = 252.0) -> double {
    if (returns.size() < 2) {
        return 0.0;
    }

    const auto n = static_cast<Eigen::Index>(returns.size());
    Eigen::Map<const Eigen::VectorXd> r(returns.data(), n);

    // Calculate cumulative return
    double cum_return = 1.0;
    for (const double ret : returns) {
        cum_return *= (1.0 + ret);
    }
    cum_return -= 1.0;

    // Annualize
    double years = static_cast<double>(returns.size()) / periods_per_year;
    double ann_return = std::pow(1.0 + cum_return, 1.0 / years) - 1.0;

    double mdd = max_drawdown(returns);
    if (mdd < 1e-10) {
        return 0.0;
    }

    return ann_return / mdd;
}

/// Calculate annualized volatility from returns
/// @param returns Period returns
/// @param periods_per_year Number of periods per year
/// @return Annualized volatility
auto annualized_volatility(span<const double> returns, double periods_per_year = 252.0) -> double {
    if (returns.size() < 2) {
        return 0.0;
    }

    const auto n = static_cast<Eigen::Index>(returns.size());
    Eigen::Map<const Eigen::VectorXd> r(returns.data(), n);

    double mean = r.mean();
    Eigen::VectorXd centered = r.array() - mean;
    double std_dev = std::sqrt(centered.squaredNorm() / static_cast<double>(n - 1));

    return std_dev * std::sqrt(periods_per_year);
}

/// Calculate comprehensive return metrics
/// @param returns Period returns
/// @param risk_free_rate Risk-free rate per period
/// @param periods_per_year Number of periods per year
/// @return ReturnMetrics with all statistics
auto calculate_return_metrics(span<const double> returns, double risk_free_rate = 0.0,
                              double periods_per_year = 252.0) -> ReturnMetrics {
    ReturnMetrics result;
    result.observations = returns.size();

    if (returns.size() < 2) {
        return result;
    }

    const auto n = static_cast<Eigen::Index>(returns.size());
    Eigen::Map<const Eigen::VectorXd> r(returns.data(), n);

    // Total return
    result.total_return = 1.0;
    for (const double ret : returns) {
        result.total_return *= (1.0 + ret);
    }
    result.total_return -= 1.0;

    // Annualized return (CAGR)
    double years = static_cast<double>(returns.size()) / periods_per_year;
    result.annualized_return = std::pow(1.0 + result.total_return, 1.0 / years) - 1.0;

    // Volatility
    double mean = r.mean();
    Eigen::VectorXd centered = r.array() - mean;
    double variance = centered.squaredNorm() / static_cast<double>(n - 1);
    result.volatility = std::sqrt(variance * periods_per_year);

    // Sharpe ratio
    double period_std = std::sqrt(variance);
    if (period_std > 1e-10) {
        result.sharpe_ratio = ((mean - risk_free_rate) / period_std) * std::sqrt(periods_per_year);
    }

    // Downside volatility and Sortino
    double sum_sq_downside = 0.0;
    for (Eigen::Index i = 0; i < n; ++i) {
        if (r(i) < 0) {
            sum_sq_downside += r(i) * r(i);
        }
    }
    double downside_var = sum_sq_downside / static_cast<double>(n);
    result.downside_vol = std::sqrt(downside_var * periods_per_year);

    if (result.downside_vol > 1e-10) {
        result.sortino_ratio = (mean / std::sqrt(downside_var)) * std::sqrt(periods_per_year);
    }

    // Max drawdown and Calmar
    result.max_drawdown = max_drawdown(returns);
    if (result.max_drawdown > 1e-10) {
        result.calmar_ratio = result.annualized_return / result.max_drawdown;
    }

    // Skewness: E[(X - mean)^3] / std^3
    if (period_std > 1e-10) {
        double sum_cubed = 0.0;
        for (Eigen::Index i = 0; i < n; ++i) {
            double z = centered(i) / period_std;
            sum_cubed += z * z * z;
        }
        result.skewness = sum_cubed / static_cast<double>(n);
    }

    // Kurtosis: E[(X - mean)^4] / std^4 - 3 (excess kurtosis)
    if (period_std > 1e-10) {
        double sum_fourth = 0.0;
        for (Eigen::Index i = 0; i < n; ++i) {
            double z = centered(i) / period_std;
            sum_fourth += z * z * z * z;
        }
        result.kurtosis = sum_fourth / static_cast<double>(n) - 3.0;
    }

    return result;
}

} // namespace finkit::stats
