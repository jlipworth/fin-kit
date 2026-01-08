/// @file risk-types.cppm
/// @brief Risk types - limits, breaches, events
///
/// Core types for risk management configuration and monitoring.

module;

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

export module finkit.risk:types;

import finkit.types;
import finkit.trading;

export namespace finkit::risk {

using std::map;
using std::optional;
using std::string;
using std::vector;

using finkit::trading::Fill;
using finkit::trading::Order;
using finkit::trading::Timestamp;
using finkit::types::Currency;

// ============================================================================
// Risk Check Results
// ============================================================================

enum class RiskDecision { Allow, Reduce, Reject };

/// Result of a pre-trade or post-trade risk check
struct RiskCheckResult {
    RiskDecision decision{RiskDecision::Allow};
    optional<double> adjusted_quantity; // For Reduce decision
    string reason;
    vector<string> violated_limits;
    bool conflicts_with_liquidation{false};
};

// ============================================================================
// Risk Events
// ============================================================================

/// Risk breach event
struct RiskBreachEvent {
    string limit_name;
    string symbol; // Empty for portfolio-level
    double current_value{0.0};
    double limit_value{0.0};
    double breach_amount{0.0};
    enum class Severity { Warning, Breach, Critical } severity{Severity::Breach};
    Timestamp timestamp;
};

/// Risk action event
struct RiskActionEvent {
    enum class Action { Alert, Reduce, Liquidate, Halt } action{Action::Alert};
    string limit_name;
    vector<string> affected_symbols;
    vector<Order> generated_orders;
    string reason;
    Timestamp timestamp;
};

// ============================================================================
// Limit Configuration
// ============================================================================

/// Per-symbol position limits
struct PositionLimits {
    double max_quantity{0};            // Max shares/contracts (0 = unlimited)
    double max_notional{0};            // Max $ value (0 = unlimited)
    double max_concentration_pct{1.0}; // Max % of NAV in single position
};

/// Portfolio-level exposure limits
struct PortfolioLimits {
    double max_gross_exposure{0}; // Sum of absolute notionals
    double max_net_exposure{0};   // |Long - Short|
    double max_leverage{0};       // Gross / NAV
    double max_long_exposure{0};
    double max_short_exposure{0};
};

/// P&L-based limits
struct PnLLimits {
    double max_drawdown_pct{1.0};      // From high-water mark (1.0 = 100%)
    double max_daily_loss_pct{1.0};    // Daily loss limit
    double max_trade_loss_pct{1.0};    // Per-trade stop loss
    double max_position_loss_pct{1.0}; // Per-position stop loss
};

/// Value at Risk limits
struct VaRLimits {
    double max_var_1d_99{0};  // 1-day 99% VaR limit
    double max_var_10d_99{0}; // 10-day 99% VaR limit
    double max_expected_shortfall{0};
};

/// Greeks/sensitivity limits
struct GreeksLimits {
    double max_delta{0}; // Net delta
    double max_gamma{0};
    double max_vega{0};
    double max_theta{0};
    double max_dv01{0}; // Rate sensitivity
};

/// Combined risk limits
struct RiskLimits {
    map<string, PositionLimits> position_limits; // Per-symbol or default
    PositionLimits default_position_limits;      // Fallback for unlisted symbols
    PortfolioLimits portfolio;
    PnLLimits pnl;
    VaRLimits var;
    GreeksLimits greeks;
};

// ============================================================================
// Breach Policies
// ============================================================================

enum class BreachPolicy {
    Continue,  // Log warning, allow
    Reject,    // Reject trade
    Reduce,    // Reduce to fit limit
    Liquidate, // Liquidate offending position(s)
    Halt       // Stop backtest/trading
};

/// Handler for a specific limit breach
struct BreachHandler {
    string limit_name;
    BreachPolicy policy{BreachPolicy::Reject};
    optional<double> reduce_to_pct; // For Reduce: target % of limit
    bool notify_strategy{true};
};

// ============================================================================
// Risk Configuration
// ============================================================================

struct RiskConfig {
    RiskLimits limits;
    vector<BreachHandler> handlers;

    // Check timing
    bool check_pre_trade{true};
    bool check_post_trade{true};
    bool check_on_bar{true};
    int check_frequency_bars{1}; // Check every N bars
};

// ============================================================================
// Risk Metrics
// ============================================================================

/// Current portfolio risk metrics
struct RiskMetrics {
    double gross_exposure{0.0};
    double net_exposure{0.0};
    double long_exposure{0.0};
    double short_exposure{0.0};
    double leverage{0.0};
    double drawdown_pct{0.0};
    double daily_pnl_pct{0.0};
    map<string, double> position_utilization; // Symbol -> % of limit
    double var_utilization_pct{0.0};
    double dv01_utilization_pct{0.0};
};

// ============================================================================
// Portfolio State
// ============================================================================

enum class PortfolioState {
    Normal,         // Normal trading
    RiskMonitoring, // Elevated monitoring, trading allowed
    Liquidating,    // Active liquidation in progress
    Halted          // No trading allowed
};

} // namespace finkit::risk
