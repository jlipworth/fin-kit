#!/usr/bin/env python3
"""Publish mock US Treasury futures data to Redis Streams.

Simulates a live market feed with random walks around realistic prices.
Streams follow the schema from the design spec: market:futures:{code}

Usage:
    cd adapters/mock
    uv run publish_mock_data.py [--interval 1.0] [--redis-host localhost] [--redis-port 6379]
"""

import argparse
import os
import random
import time

import redis


INSTRUMENTS = {
    "TU": {"base_price": 103.25, "tick": 1 / 128},     # 2Y T-Note
    "FV": {"base_price": 107.50, "tick": 1 / 128},     # 5Y T-Note
    "TY": {"base_price": 110.75, "tick": 1 / 64},      # 10Y T-Note
    "US": {"base_price": 118.00, "tick": 1 / 32},      # 30Y T-Bond
}

MAXLEN = 10_000  # per spec: MAXLEN ~10000 approximate trimming


def main():
    parser = argparse.ArgumentParser(description="Mock treasury data publisher")
    parser.add_argument("--interval", type=float, default=1.0, help="Seconds between ticks")
    parser.add_argument("--redis-host", default=os.getenv("REDIS_HOST", "localhost"))
    parser.add_argument("--redis-port", type=int, default=int(os.getenv("REDIS_PORT", "6379")))
    args = parser.parse_args()

    r = redis.Redis(host=args.redis_host, port=args.redis_port, decode_responses=True)
    r.ping()
    print(f"Connected to Redis at {args.redis_host}:{args.redis_port}")

    prices = {code: info["base_price"] for code, info in INSTRUMENTS.items()}
    seq = 0

    try:
        while True:
            ts = int(time.time() * 1000)
            for code, info in INSTRUMENTS.items():
                # Random walk: +/- 1-3 ticks
                move = random.choice([-3, -2, -1, 0, 0, 0, 1, 2, 3]) * info["tick"]
                prices[code] = round(prices[code] + move, 6)

                stream_key = f"market:futures:{code}"
                r.xadd(
                    stream_key,
                    {"price": str(prices[code]), "timestamp": str(ts)},
                    maxlen=MAXLEN,
                    approximate=True,
                )

            seq += 1
            if seq % 10 == 0:
                print(f"[{seq}] TU={prices['TU']:.4f}  FV={prices['FV']:.4f}  "
                      f"TY={prices['TY']:.4f}  US={prices['US']:.4f}")

            # Heartbeat
            r.xadd(
                "heartbeat:mock-publisher",
                {"status": "ok", "timestamp": str(ts)},
                maxlen=1000,
                approximate=True,
            )

            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
