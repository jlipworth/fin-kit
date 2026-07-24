#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sw/redis++/redis++.h>
#include <thread>
#include <unordered_map>
#include <vector>

namespace redis = sw::redis;
using Clock = std::chrono::steady_clock;

static std::atomic<bool> running{true};
void handle_signal(int) {
    running = false;
}

// ── Configuration ──────────────────────────────────────────────────────────

struct Config {
    std::string redis_host = "localhost";
    int redis_port = 6379;
    int calc_window_ms = 100;
};

Config load_config() {
    Config c;
    if (auto* v = std::getenv("REDIS_HOST"))
        c.redis_host = v;
    if (auto* v = std::getenv("REDIS_PORT"))
        c.redis_port = std::atoi(v);
    if (auto* v = std::getenv("FINKIT_STREAM_CALC_WINDOW_MS"))
        c.calc_window_ms = std::atoi(v);
    if (c.redis_port <= 0 || c.redis_port > 65535)
        c.redis_port = 6379;
    if (c.calc_window_ms <= 0)
        c.calc_window_ms = 100;
    return c;
}

// ── Stream helpers ─────────────────────────────────────────────────────────

static const std::string GROUP = "finkit";
static const std::string CONSUMER = "finkit-1";
static constexpr long long MAXLEN = 10'000;

void ensure_group(redis::Redis& r, const std::string& stream) {
    try {
        r.xgroup_create(stream, GROUP, "$", true); // mkstream=true
    } catch (const redis::ReplyError&) {
        // BUSYGROUP — already exists
    }
}

void ensure_groups(redis::Redis& r, const std::vector<std::string>& streams) {
    for (const auto& s : streams) {
        ensure_group(r, s);
    }
}

// ── Buffered market data ───────────────────────────────────────────────────

struct FuturesQuote {
    double price;
    long long timestamp;
};

// ── Calculations ───────────────────────────────────────────────────────────

void run_calculations(redis::Redis& r,
                      const std::unordered_map<std::string, FuturesQuote>& buffer) {
    for (const auto& [code, quote] : buffer) {
        // PoC calculation: implied rate = 100 - price
        double implied_rate = 100.0 - quote.price;

        std::string stream = "calc:implied_rate:" + code;
        std::vector<std::pair<std::string, std::string>> fields = {
            {"rate", std::to_string(implied_rate)},
            {"price", std::to_string(quote.price)},
            {"timestamp", std::to_string(quote.timestamp)},
        };
        r.xadd(stream, "*", fields.begin(), fields.end(), MAXLEN, true);
    }
}

// ── Main loop ──────────────────────────────────────────────────────────────

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    auto config = load_config();

    redis::ConnectionOptions conn_opts;
    conn_opts.host = config.redis_host;
    conn_opts.port = config.redis_port;
    conn_opts.connect_timeout = std::chrono::seconds(5);
    conn_opts.socket_timeout = std::chrono::seconds(5);
    conn_opts.keep_alive = true;

    redis::ConnectionPoolOptions pool_opts;
    pool_opts.size = 1;

    std::cout << "[finkit-stream] connecting to Redis at " << config.redis_host << ":"
              << config.redis_port << "\n";
    redis::Redis r(conn_opts, pool_opts);

    // Streams to consume — treasury futures from mock publisher or LSEG adapter
    std::vector<std::string> input_streams = {
        "market:futures:TU",
        "market:futures:FV",
        "market:futures:TY",
        "market:futures:US",
    };

    ensure_groups(r, input_streams);

    std::cout << "[finkit-stream] listening on " << input_streams.size()
              << " streams (window=" << config.calc_window_ms << "ms)\n";

    auto last_calc = Clock::now();
    auto last_heartbeat = Clock::now();
    std::unordered_map<std::string, FuturesQuote> buffer;
    std::unordered_map<std::string, std::vector<std::string>> pending_acks;

    // First pass reads this consumer's pending entries (delivered but unacked
    // before a previous crash/shutdown) so they are reprocessed, then switches
    // to new-message delivery.
    bool drain_pending = true;

    while (running) {
        // Block for up to calc_window_ms
        using Item = std::pair<std::string, std::vector<std::pair<std::string, std::string>>>;
        using ItemStream = std::vector<Item>;
        std::unordered_map<std::string, ItemStream> results;

        std::vector<std::pair<std::string, std::string>> stream_ids;
        for (const auto& s : input_streams) {
            stream_ids.emplace_back(s, drain_pending ? "0" : ">");
        }

        try {
            r.xreadgroup(GROUP, CONSUMER, stream_ids.begin(), stream_ids.end(),
                         std::chrono::milliseconds(config.calc_window_ms),
                         100, // count
                         std::inserter(results, results.end()));
        } catch (const redis::ReplyError& e) {
            std::cerr << "[finkit-stream] XREADGROUP error: " << e.what() << "\n";
            // Groups vanish if Redis restarts without persisted state; recreate
            // them instead of spinning on NOGROUP forever.
            if (std::string(e.what()).find("NOGROUP") != std::string::npos) {
                try {
                    ensure_groups(r, input_streams);
                } catch (const redis::Error& ge) {
                    std::cerr << "[finkit-stream] group recreate failed: " << ge.what() << "\n";
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        } catch (const redis::Error& e) {
            std::cerr << "[finkit-stream] XREADGROUP error: " << e.what() << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        // Parse incoming messages into buffer
        std::size_t item_count = 0;
        for (const auto& [stream, items] : results) {
            // Extract instrument code from stream name: "market:futures:TY" → "TY"
            auto code = stream.substr(stream.rfind(':') + 1);

            for (const auto& [id, fields] : items) {
                ++item_count;
                FuturesQuote quote{};
                bool valid = true;
                for (const auto& [key, val] : fields) {
                    try {
                        if (key == "price")
                            quote.price = std::stod(val);
                        if (key == "timestamp")
                            quote.timestamp = std::stoll(val);
                    } catch (const std::exception& e) {
                        std::cerr << "[finkit-stream] malformed field " << key << "=" << val
                                  << " in " << stream << ": " << e.what() << "\n";
                        valid = false;
                        break;
                    }
                }
                if (valid)
                    buffer[code] = quote;

                pending_acks[stream].push_back(id);
            }
        }

        if (drain_pending && item_count == 0) {
            drain_pending = false;
        }

        // Fire calculations when window expires, then ACK processed messages.
        // ACKs are flushed even when the window held only malformed messages,
        // and retained for retry when calculations or the ACK itself fail —
        // at-least-once for calc outputs (redelivery may duplicate them).
        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_calc);
        if (elapsed.count() >= config.calc_window_ms &&
            (!buffer.empty() || !pending_acks.empty())) {
            bool calc_ok = true;
            if (!buffer.empty()) {
                try {
                    run_calculations(r, buffer);
                } catch (const std::exception& e) {
                    std::cerr << "[finkit-stream] calculation error: " << e.what() << "\n";
                    calc_ok = false;
                }
            }
            last_calc = now;

            if (calc_ok) {
                buffer.clear();
                try {
                    for (const auto& [stream, ids] : pending_acks) {
                        for (const auto& id : ids) {
                            r.xack(stream, GROUP, id);
                        }
                    }
                    pending_acks.clear();
                } catch (const redis::Error& e) {
                    // Partial ACK is fine — re-acking an id is a no-op; retry
                    // the batch next window.
                    std::cerr << "[finkit-stream] XACK error: " << e.what() << "\n";
                }
            }
        }

        // Heartbeat every 2 seconds
        auto hb_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat);
        if (hb_elapsed.count() >= 2) {
            auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
            std::vector<std::pair<std::string, std::string>> hb_fields = {
                {"status", "ok"},
                {"timestamp", std::to_string(ts)},
            };
            try {
                r.xadd("heartbeat:finkit-stream", "*", hb_fields.begin(), hb_fields.end(), 1000LL,
                       true);
            } catch (const redis::Error& e) {
                std::cerr << "[finkit-stream] heartbeat error: " << e.what() << "\n";
            }
            last_heartbeat = now;
        }
    }

    std::cout << "\n[finkit-stream] shutting down\n";
    return 0;
}
