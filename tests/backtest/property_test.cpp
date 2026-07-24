/// @file property_test.cpp
/// @brief Seeded property / invariant tests for the backtest framework.
///
/// Every test drives a std::mt19937 seeded with a FIXED base seed and derives a
/// distinct per-iteration seed (base + iteration). No wall-clock or true
/// randomness is used, so a failure is exactly reproducible: the diagnostic
/// prints the seed and iteration, and re-running seeds the same sequence.
///
/// On an invariant violation the test emits the seed, the iteration, and the
/// minimal state needed to reproduce and understand the breach, then fails.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <ql/quantlib.hpp>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

import finkit.backtest;
import finkit.trading;
import finkit.risk;
import finkit.types;

namespace {

using namespace finkit::backtest;
using namespace finkit::trading;
using namespace finkit::risk;
using finkit::types::Currency;
namespace ql = QuantLib;

// ============================================================================
// Shared helpers
// ============================================================================

/// Round to cents so fills use exact 2-decimal prices/commissions; this keeps
/// float accumulation well under the 1e-6 tolerance across a bounded sequence.
auto round2(double x) -> double { return std::round(x * 100.0) / 100.0; }

/// Fixed base timestamp (2024-01-02, local time — matches the other test files).
auto base_ts() -> Timestamp {
    std::tm tm{};
    tm.tm_year = 2024 - 1900;
    tm.tm_mon = 0;
    tm.tm_mday = 2;
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(tp);
}

/// `base` advanced by whole calendar days (24h steps).
auto ts_plus_days(Timestamp base, int days) -> Timestamp {
    return base + std::chrono::hours(24 * days);
}

auto side_name(OrderSide s) -> const char* { return s == OrderSide::Buy ? "BUY" : "SELL"; }

// ============================================================================
// Property 1 — Portfolio invariants under random fill sequences
// ============================================================================
//
// Two invariants are checked after EVERY fill of a randomly generated sequence
// (random symbols, buys/sells including flips and zero-crossings, random
// prices/quantities/commissions, and occasional mark-to-market):
//
//   (I1)  cash + market_value == nav                       (tolerance 1e-6)
//   (I2)  Σ realized_pnl + Σ unrealized_pnl
//                == nav - initial_cash + Σ commissions      (tolerance 1e-6)
//
// where market_value = Σ_pos quantity·market_price and nav = Σ cash + market_value.
//
// --- Derivation of (I2) from the code (Portfolio::apply_fill / ::nav) --------
// Portfolio::apply_fill moves cash by the signed fill value minus commission:
//     buy:  cash -= price·qty + commission
//     sell: cash += price·qty - commission
// so after any number of fills (single currency):
//     cash = initial_cash + Σ_fills(sell:+price·qty, buy:-price·qty) - Σ commission.
//
// The per-symbol realized/quantity/avg_cost bookkeeping in apply_fill maintains
// the invariant (proved by induction over opens, partial closes and
// zero-crossings — and checked numerically before this test was written):
//     Σ_fills(sell:+price·qty, buy:-price·qty)_sym
//         == realized_pnl_sym - quantity_sym·avg_cost_sym.
// Base case (open from flat): a buy of q@p gives cash-flow -p·q and leaves
// realized 0, quantity q, avg p ⇒ -q·p = 0 - q·p. Each subsequent open, close,
// or zero-crossing preserves it (the closed leg contributes (p-avg)·closed to
// both realized and the cash-flow; the crossing leg re-bases avg to p). Summing
// over symbols:
//     Σ cash-flow = Σ realized - Σ quantity·avg_cost.
// Therefore
//     nav = Σ cash + Σ quantity·market_price
//         = initial_cash + Σ cash-flow - Σ commission + Σ quantity·market_price
//         = initial_cash + Σ realized - Σ quantity·avg_cost - Σ commission
//                        + Σ quantity·market_price
//         = initial_cash + Σ realized + Σ quantity·(market_price - avg_cost)
//                        - Σ commission
//         = initial_cash + Σ realized + Σ unrealized_pnl - Σ commission,
// which rearranges to exactly (I2). Note market_price enters nav and the
// unrealized term identically, so (I2) holds for any mark-to-market prices.
// ----------------------------------------------------------------------------

/// Runs one random fill sequence. Returns "" on success, or a diagnostic string
/// (including the offending fill and state) on the first invariant violation.
auto run_portfolio_scenario(uint32_t seed, int n_fills) -> std::string {
    std::mt19937 gen(seed);
    const double initial_cash = 100000.0;

    Portfolio pf(Currency::USD);
    pf.deposit(initial_cash, Currency::USD);
    StaticFXProvider fx; // identity conversion within a single currency

    const std::vector<std::string> syms = {"AAA", "BBB", "CCC"};
    std::uniform_int_distribution<size_t> sym_d(0, syms.size() - 1);
    std::uniform_int_distribution<int> side_d(0, 1);
    std::uniform_int_distribution<int> qty_d(1, 100);
    std::uniform_real_distribution<double> price_d(1.0, 200.0);
    std::uniform_real_distribution<double> comm_d(0.0, 5.0);
    std::uniform_int_distribution<int> mark_d(0, 3); // ~25% of steps also re-mark

    double total_commission = 0.0;
    const Timestamp t0 = base_ts();

    for (int i = 0; i < n_fills; ++i) {
        // Occasionally re-mark a random symbol to a fresh price to decouple
        // market_price from the last fill price (still must satisfy both
        // invariants).
        if (mark_d(gen) == 0) {
            pf.mark_to_market(syms[sym_d(gen)], round2(price_d(gen)), ts_plus_days(t0, i));
        }

        const std::string& sym = syms[sym_d(gen)];
        const OrderSide side = side_d(gen) != 0 ? OrderSide::Sell : OrderSide::Buy;
        const double qty = static_cast<double>(qty_d(gen));
        const double price = round2(price_d(gen));
        const double comm = round2(comm_d(gen));

        Fill fill{.order_id = OrderId{static_cast<uint64_t>(i + 1)},
                  .fill_id = static_cast<uint64_t>(i + 1),
                  .symbol = sym,
                  .side = side,
                  .quantity = qty,
                  .price = price,
                  .commission = comm,
                  .fill_time = ts_plus_days(t0, i),
                  .is_partial = false,
                  .cumulative_filled = qty};
        pf.apply_fill(fill);
        total_commission += comm;

        // Aggregate portfolio state.
        const double cash = pf.cash(Currency::USD);
        double market_value = 0.0;
        double realized = 0.0;
        double unrealized = 0.0;
        for (const auto& [s, p] : pf.positions()) {
            market_value += p.quantity * p.market_price;
            realized += p.realized_pnl;
            unrealized += p.unrealized_pnl();
        }
        const double nav = pf.nav(fx);

        const double bal_err = std::abs(cash + market_value - nav);
        const double pnl_lhs = realized + unrealized;
        const double pnl_rhs = nav - initial_cash + total_commission;
        const double pnl_err = std::abs(pnl_lhs - pnl_rhs);

        if (bal_err > 1e-6 || pnl_err > 1e-6) {
            std::ostringstream os;
            os << "  fill #" << i << ": " << side_name(side) << " " << qty << " " << sym << " @ "
               << price << " comm " << comm << "\n"
               << "  cash=" << cash << " market_value=" << market_value << " nav=" << nav << "\n"
               << "  (I1) |cash+mv-nav| = " << bal_err << "\n"
               << "  realized=" << realized << " unrealized=" << unrealized
               << " total_commission=" << total_commission << "\n"
               << "  (I2) |(realized+unrealized) - (nav-initial+comm)| = " << pnl_err
               << "  [lhs=" << pnl_lhs << " rhs=" << pnl_rhs << "]";
            return os.str();
        }
    }
    return {};
}

TEST(PropertyTest, PortfolioInvariantsUnderRandomFills) {
    constexpr uint32_t kBaseSeed = 0x00C0FFEEu;
    constexpr int kIterations = 400;
    constexpr int kFillsPerRun = 50;

    for (int it = 0; it < kIterations; ++it) {
        const uint32_t seed = kBaseSeed + static_cast<uint32_t>(it);
        std::string err = run_portfolio_scenario(seed, kFillsPerRun);
        if (!err.empty()) {
            FAIL() << "Portfolio invariant violated at iteration " << it << " (seed=" << seed
                   << "):\n"
                   << err;
        }
    }
}

// ============================================================================
// Property 2 — Full BacktestEngine run with a random-order strategy
// ============================================================================

/// Submits random market orders (random side/quantity) for a single symbol with
/// probability `submit_prob`, but never on or after `stop_before` (its 0-based
/// bar index) so that every accepted order is guaranteed a later same-symbol bar
/// to fill against. Also observes the high-water mark for monotonicity and sums
/// commissions actually charged (via on_fill).
class RandomOrderStrategy : public IStrategy {
public:
    RandomOrderStrategy(std::string sym, uint32_t seed, int stop_before, int max_qty,
                        double submit_prob)
        : sym_(std::move(sym)), gen_(seed), stop_before_(stop_before), max_qty_(max_qty),
          submit_prob_(submit_prob) {}

    auto name() const -> std::string override { return "RandomOrder"; }
    auto id() const -> std::string override { return "rand_order_" + sym_; }

    void on_start(StrategyContext& ctx) override {
        prev_hwm_ = ctx.portfolio.high_water_mark();
    }

    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        ++bars_seen_;

        // High-water-mark monotonicity (observed before the engine folds this
        // bar into the HWM ⇒ a non-decreasing subsequence of the true series).
        const double hwm = ctx.portfolio.high_water_mark();
        if (!hwm_violation_ && hwm + 1e-6 < prev_hwm_) {
            hwm_violation_ = true;
            hwm_prev_ = prev_hwm_;
            hwm_cur_ = hwm;
            hwm_bar_ = bars_seen_;
        }
        prev_hwm_ = hwm;

        if (bar.symbol == sym_ && bar_index_ < stop_before_) {
            std::uniform_real_distribution<double> u(0.0, 1.0);
            if (u(gen_) < submit_prob_) {
                std::uniform_int_distribution<int> side_d(0, 1);
                std::uniform_int_distribution<int> qty_d(1, max_qty_);
                const OrderSide side = side_d(gen_) != 0 ? OrderSide::Sell : OrderSide::Buy;
                const double qty = static_cast<double>(qty_d(gen_));
                ctx.submit_order(make_market_order(sym_, side, qty, id()));
                ++orders_submitted_;
            }
        }
        ++bar_index_;
    }

    void on_fill(const Fill& fill, StrategyContext& /*ctx*/) override {
        total_commission_ += fill.commission;
        ++fills_observed_;
        if (fill.order_id == OrderId{0}) {
            ++synthetic_fills_;
        }
    }

    // Accessors used by the test body.
    int orders_submitted() const { return orders_submitted_; }
    int bars_seen() const { return bars_seen_; }
    int synthetic_fills() const { return synthetic_fills_; }
    double total_commission() const { return total_commission_; }
    bool hwm_violation() const { return hwm_violation_; }
    double hwm_prev() const { return hwm_prev_; }
    double hwm_cur() const { return hwm_cur_; }
    int hwm_bar() const { return hwm_bar_; }

private:
    std::string sym_;
    std::mt19937 gen_;
    int stop_before_;
    int max_qty_;
    double submit_prob_;

    int bar_index_{0};
    int bars_seen_{0};
    int orders_submitted_{0};
    int fills_observed_{0};
    int synthetic_fills_{0};
    double total_commission_{0.0};

    double prev_hwm_{0.0};
    bool hwm_violation_{false};
    double hwm_prev_{0.0};
    double hwm_cur_{0.0};
    int hwm_bar_{0};
};

TEST(PropertyTest, FullEngineRunInvariants) {
    constexpr uint32_t kBaseSeed = 0x0BADF00Du;
    constexpr int kIterations = 300;

    for (int it = 0; it < kIterations; ++it) {
        const uint32_t seed = kBaseSeed + static_cast<uint32_t>(it);
        std::mt19937 gen(seed);

        std::uniform_int_distribution<int> nbars_d(6, 40);
        std::uniform_real_distribution<double> price_d(10.0, 200.0);
        std::uniform_real_distribution<double> comm_d(0.0, 0.05);
        std::uniform_real_distribution<double> slip_d(0.0, 2.0);
        const int n_bars = nbars_d(gen);
        const double commission_per_share = round2(comm_d(gen));
        const double slippage_bps = slip_d(gen);

        BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                              .end_date = ql::Date(31, ql::December, 2024),
                              .initial_capital = 1000000.0,
                              .base_currency = Currency::USD,
                              .slippage_bps = slippage_bps,
                              .commission_per_share = commission_per_share,
                              .commission_pct = 0.0};
        // A position cap so both the Reduce and Reject pre-trade paths are hit;
        // it also bounds |position| so NAV stays comfortably positive (⇒ result
        // drawdowns land in [0,1]).
        config.risk.limits.default_position_limits.max_quantity = 100.0;

        const std::string sym = "SYM";
        auto feed = std::make_unique<InMemoryDataFeed>();
        const Timestamp t0 = base_ts();
        for (int b = 0; b < n_bars; ++b) {
            const double px = round2(price_d(gen));
            feed->add_bar(BarEvent{.symbol = sym,
                                   .timestamp = ts_plus_days(t0, b),
                                   .open = px,
                                   .high = px,
                                   .low = px,
                                   .close = px,
                                   .volume = 1000.0});
        }

        BacktestEngine engine(config);
        engine.set_data_feed(std::move(feed));
        // stop_before = n_bars-1: no order is submitted on the final bar, so
        // every accepted order has a subsequent same-symbol bar to fill on.
        auto strat_owned =
            std::make_unique<RandomOrderStrategy>(sym, seed ^ 0x9E3779B9u, n_bars - 1, 80, 0.6);
        auto* strat = strat_owned.get();
        engine.add_strategy(std::move(strat_owned));

        auto r = engine.run();
        const auto& pf = engine.get_portfolio();
        StaticFXProvider fx;

        auto fail_ctx = [&](const std::string& what) {
            std::ostringstream os;
            os << "iteration " << it << " (seed=" << seed << ", n_bars=" << n_bars
               << ", comm/sh=" << commission_per_share << ", slip=" << slippage_bps << "): " << what
               << "\n  submitted=" << strat->orders_submitted() << " total_trades=" << r.total_trades
               << " orders_rejected=" << r.orders_rejected
               << " forced_liquidations=" << r.forced_liquidations
               << " synthetic_fills=" << strat->synthetic_fills()
               << " final_nav=" << r.final_nav;
            return os.str();
        };

        // No synthetic settlement trades in this configuration, so total_trades
        // is exactly the count of genuine order fills.
        ASSERT_EQ(strat->synthetic_fills(), 0) << fail_ctx("unexpected synthetic (order_id 0) fill");
        ASSERT_EQ(r.forced_liquidations, 0) << fail_ctx("unexpected forced liquidation");
        ASSERT_TRUE(r.delisting_events.empty()) << fail_ctx("unexpected delisting event");

        // (A) Order accounting: filled + rejected == submitted.
        ASSERT_EQ(r.total_trades + r.orders_rejected, strat->orders_submitted())
            << fail_ctx("orders_filled + orders_rejected != orders_submitted");

        // (B) Every bar was processed (monotone timestamps, no listing windows).
        ASSERT_EQ(r.out_of_order_bars_dropped, 0) << fail_ctx("bar dropped by look-ahead guard");
        ASSERT_EQ(r.inactive_symbol_bars_dropped, 0) << fail_ctx("bar dropped by universe filter");
        ASSERT_EQ(strat->bars_seen(), n_bars) << fail_ctx("processed-bar count != bars submitted");

        // (C) Final NAV consistent with portfolio state.
        double market_value = 0.0;
        for (const auto& [s, p] : pf.positions()) {
            market_value += p.quantity * p.market_price;
        }
        const double recomputed_nav = pf.nav(fx);
        const double cash = pf.cash(Currency::USD);
        ASSERT_LE(std::abs(r.final_nav - recomputed_nav), 1e-6)
            << fail_ctx("result.final_nav != portfolio.nav()");
        ASSERT_LE(std::abs(cash + market_value - recomputed_nav), 1e-6)
            << fail_ctx("cash + market_value != nav");

        // (C') P&L identity on the engine's portfolio (borrow-free ⇒ commissions
        // are the only non-price cash leak). Same identity as Property 1.
        double realized = 0.0;
        double unrealized = 0.0;
        for (const auto& [s, p] : pf.positions()) {
            realized += p.realized_pnl;
            unrealized += p.unrealized_pnl();
        }
        const double pnl_lhs = realized + unrealized;
        const double pnl_rhs = r.final_nav - config.initial_capital + strat->total_commission();
        ASSERT_LE(std::abs(pnl_lhs - pnl_rhs), 1e-4)
            << fail_ctx("realized+unrealized != nav - initial + commissions");

        // (D) Drawdown metrics in [0,1]; max >= mean.
        ASSERT_GE(r.max_drawdown_pct, 0.0) << fail_ctx("max_drawdown_pct < 0");
        ASSERT_LE(r.max_drawdown_pct, 1.0) << fail_ctx("max_drawdown_pct > 1");
        ASSERT_GE(r.avg_drawdown_pct, 0.0) << fail_ctx("avg_drawdown_pct < 0");
        ASSERT_LE(r.avg_drawdown_pct, 1.0) << fail_ctx("avg_drawdown_pct > 1");
        ASSERT_LE(r.avg_drawdown_pct, r.max_drawdown_pct + 1e-12)
            << fail_ctx("avg_drawdown_pct > max_drawdown_pct");

        // (E) High-water mark monotone non-decreasing.
        if (strat->hwm_violation()) {
            FAIL() << fail_ctx("high-water mark decreased") << "\n  at bar " << strat->hwm_bar()
                   << ": prev_hwm=" << strat->hwm_prev() << " cur_hwm=" << strat->hwm_cur();
        }
    }
}

// ============================================================================
// Property 3 — Realism invariants (listing windows & borrow costs)
// ============================================================================

/// On each bar, with probability `submit_prob`, submits a random market order
/// (side per `allow_short`: both sides if shorting is allowed, buys only
/// otherwise) for the bar's own symbol. Records every fill's (symbol, time,
/// synthetic?) and whether any position was ever short.
class RandomRealismStrategy : public IStrategy {
public:
    RandomRealismStrategy(uint32_t seed, double submit_prob, bool allow_short)
        : gen_(seed), submit_prob_(submit_prob), allow_short_(allow_short) {}

    auto name() const -> std::string override { return "RandomRealism"; }
    auto id() const -> std::string override { return "rand_realism"; }

    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        for (const auto& [s, p] : ctx.portfolio.positions()) {
            if (p.quantity < -1e-10) {
                any_short_ = true;
            }
        }
        std::uniform_real_distribution<double> u(0.0, 1.0);
        if (u(gen_) < submit_prob_) {
            std::uniform_int_distribution<int> qty_d(1, 50);
            OrderSide side = OrderSide::Buy;
            if (allow_short_) {
                std::uniform_int_distribution<int> side_d(0, 1);
                side = side_d(gen_) != 0 ? OrderSide::Sell : OrderSide::Buy;
            }
            ctx.submit_order(make_market_order(bar.symbol, side, static_cast<double>(qty_d(gen_)),
                                               id()));
        }
    }

    void on_fill(const Fill& fill, StrategyContext& /*ctx*/) override {
        fills_.push_back({fill.symbol, fill.fill_time, fill.order_id == OrderId{0}});
    }

    struct FillRec {
        std::string symbol;
        Timestamp time;
        bool synthetic;
    };
    const std::vector<FillRec>& fills() const { return fills_; }
    bool any_short() const { return any_short_; }

private:
    std::mt19937 gen_;
    double submit_prob_;
    bool allow_short_;
    std::vector<FillRec> fills_;
    bool any_short_{false};
};

// --- 3a: no market fill ever occurs outside a symbol's listing window --------
//
// The engine's universe filter drops bars outside [listed_from, delisted_at)
// before they reach execution, so genuine (execution) fills must land inside the
// window. The synthetic delisting settlement (order_id 0) is the designed
// boundary event and is validated separately (it fires at time >= delisted_at).
TEST(PropertyTest, RealismListingWindowInvariants) {
    constexpr uint32_t kBaseSeed = 0x5EED1234u;
    constexpr int kIterations = 300;
    const std::vector<std::string> syms = {"S0", "S1", "S2", "S3"};

    for (int it = 0; it < kIterations; ++it) {
        const uint32_t seed = kBaseSeed + static_cast<uint32_t>(it);
        std::mt19937 gen(seed);
        const Timestamp t0 = base_ts();
        const int span_days = 30;

        // Random per-symbol listing windows (some open-ended on either side).
        std::map<std::string, ListingWindow> windows;
        std::uniform_int_distribution<int> from_d(0, 8);
        std::uniform_int_distribution<int> len_d(3, 18);
        std::uniform_int_distribution<int> opt_d(0, 4);
        for (const auto& s : syms) {
            ListingWindow w;
            if (opt_d(gen) != 0) {
                w.listed_from = ts_plus_days(t0, from_d(gen));
            }
            if (opt_d(gen) != 0) {
                const int start = w.listed_from ? from_d(gen) : 0;
                w.delisted_at = ts_plus_days(t0, start + len_d(gen));
            }
            windows[s] = w;
        }

        BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                              .end_date = ql::Date(31, ql::December, 2024),
                              .initial_capital = 1000000.0,
                              .base_currency = Currency::USD,
                              .slippage_bps = 0.0,
                              .commission_per_share = 0.0,
                              .commission_pct = 0.0};
        config.listing_windows = windows;

        // One bar per (day, random subset of symbols); all bars on a given day
        // share a timestamp (equal timestamps are allowed) and days strictly
        // increase, so the look-ahead guard never fires.
        auto feed = std::make_unique<InMemoryDataFeed>();
        std::uniform_int_distribution<int> include_d(0, 1);
        std::uniform_real_distribution<double> price_d(10.0, 200.0);
        for (int d = 0; d < span_days; ++d) {
            for (const auto& s : syms) {
                if (include_d(gen) == 0) {
                    continue;
                }
                const double px = round2(price_d(gen));
                feed->add_bar(BarEvent{.symbol = s,
                                       .timestamp = ts_plus_days(t0, d),
                                       .open = px,
                                       .high = px,
                                       .low = px,
                                       .close = px,
                                       .volume = 1000.0});
            }
        }

        BacktestEngine engine(config);
        engine.set_data_feed(std::move(feed));
        auto strat_owned = std::make_unique<RandomRealismStrategy>(seed ^ 0x51ED2A5Au, 0.5,
                                                                   /*allow_short=*/true);
        auto* strat = strat_owned.get();
        engine.add_strategy(std::move(strat_owned));
        engine.run();

        for (const auto& f : strat->fills()) {
            const auto& w = windows.at(f.symbol);
            if (!f.synthetic) {
                // Genuine market fill: must be strictly inside the window.
                const bool after_listing = !w.listed_from || f.time >= *w.listed_from;
                const bool before_delist = !w.delisted_at || f.time < *w.delisted_at;
                if (!after_listing || !before_delist) {
                    FAIL() << "iteration " << it << " (seed=" << seed
                           << "): market fill outside listing window for " << f.symbol
                           << "\n  fill_time_days_from_base="
                           << std::chrono::duration_cast<std::chrono::hours>(f.time - t0).count() /
                                  24
                           << " listed_from_set=" << (w.listed_from ? 1 : 0)
                           << " delisted_at_set=" << (w.delisted_at ? 1 : 0);
                }
            } else {
                // Synthetic delisting settlement: fires at/after the delist edge.
                ASSERT_TRUE(w.delisted_at.has_value())
                    << "iteration " << it << " (seed=" << seed
                    << "): synthetic fill for a symbol with no delist date: " << f.symbol;
                ASSERT_GE(f.time, *w.delisted_at)
                    << "iteration " << it << " (seed=" << seed
                    << "): synthetic settlement before delist edge for " << f.symbol;
            }
        }
    }
}

// --- 3b: borrow cost is non-negative, and exactly 0 when no short occurs ------
TEST(PropertyTest, RealismBorrowCostInvariants) {
    constexpr uint32_t kBaseSeed = 0xB0110C05u;
    constexpr int kIterations = 300;
    const std::vector<std::string> syms = {"S0", "S1", "S2"};

    for (int it = 0; it < kIterations; ++it) {
        const uint32_t seed = kBaseSeed + static_cast<uint32_t>(it);
        std::mt19937 gen(seed);
        const Timestamp t0 = base_ts();
        const int n_days = 24;

        // Half the iterations are long-only (guaranteeing no short ⇒ the "== 0"
        // branch is always exercised); the other half allow shorts.
        const bool allow_short = (it % 2 == 1);

        // Random borrow config (a mix of zero and positive rates, some HTB off).
        BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                              .end_date = ql::Date(31, ql::December, 2024),
                              .initial_capital = 1000000.0,
                              .base_currency = Currency::USD,
                              .slippage_bps = 0.0,
                              .commission_per_share = 0.0,
                              .commission_pct = 0.0};
        std::uniform_int_distribution<int> rate_pick(0, 2);
        std::uniform_real_distribution<double> rate_d(1.0, 500.0);
        for (const auto& s : syms) {
            const double rate = rate_pick(gen) == 0 ? 0.0 : rate_d(gen);
            config.borrow[s] = BorrowConfig{.borrow_rate_bps = rate, .hard_to_borrow = false};
        }

        auto feed = std::make_unique<InMemoryDataFeed>();
        std::uniform_int_distribution<size_t> sym_d(0, syms.size() - 1);
        std::uniform_real_distribution<double> price_d(10.0, 200.0);
        for (int d = 0; d < n_days; ++d) {
            // One symbol's bar per day (keeps day transitions ⇒ borrow accrual).
            const std::string& s = syms[sym_d(gen)];
            const double px = round2(price_d(gen));
            feed->add_bar(BarEvent{.symbol = s,
                                   .timestamp = ts_plus_days(t0, d),
                                   .open = px,
                                   .high = px,
                                   .low = px,
                                   .close = px,
                                   .volume = 1000.0});
        }

        BacktestEngine engine(config);
        engine.set_data_feed(std::move(feed));
        auto strat_owned =
            std::make_unique<RandomRealismStrategy>(seed ^ 0xB05505A5u, 0.6, allow_short);
        auto* strat = strat_owned.get();
        engine.add_strategy(std::move(strat_owned));
        auto r = engine.run();

        // Always non-negative.
        if (r.borrow_cost_paid < 0.0) {
            FAIL() << "iteration " << it << " (seed=" << seed
                   << ", allow_short=" << allow_short
                   << "): borrow_cost_paid < 0: " << r.borrow_cost_paid;
        }
        // Exactly 0 whenever no short position ever existed.
        if (!strat->any_short() && r.borrow_cost_paid != 0.0) {
            FAIL() << "iteration " << it << " (seed=" << seed
                   << ", allow_short=" << allow_short
                   << "): no short occurred yet borrow_cost_paid=" << r.borrow_cost_paid;
        }
    }
}

} // namespace
