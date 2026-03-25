#!/usr/bin/env python3
"""LSEG -> Redis Streams adapter for US Treasury futures.

Polls LSEG via jl-lseg-toolkit for the latest treasury futures prices
and publishes to Redis Streams for consumption by fin-kit and the dashboard.

Requirements:
    - LSEG Workspace running on localhost:9000 with valid app key
    - jl-lseg-toolkit submodule initialized
    - Redis running

Usage:
    cd adapters/lseg
    uv sync
    uv run ingest.py [--interval 5] [--redis-host localhost] [--redis-port 6379]
"""

import argparse
import os
import sys
import time

import redis
from lseg_toolkit.timeseries import LSEGDataClient

MAXLEN = 10_000

# CME product code -> LSEG RIC mapping (continuous front-month contracts)
INSTRUMENTS = {
    "TU": "TUc1",  # 2Y T-Note
    "FV": "FVc1",  # 5Y T-Note
    "TY": "TYc1",  # 10Y T-Note
    "US": "USc1",  # 30Y T-Bond
}


def main():
    parser = argparse.ArgumentParser(description="LSEG treasury adapter")
    parser.add_argument(
        "--interval",
        type=float,
        default=5.0,
        help="Seconds between polls (default: 5)",
    )
    parser.add_argument(
        "--redis-host", default=os.getenv("REDIS_HOST", "localhost")
    )
    parser.add_argument(
        "--redis-port",
        type=int,
        default=int(os.getenv("REDIS_PORT", "6379")),
    )
    args = parser.parse_args()

    r = redis.Redis(
        host=args.redis_host, port=args.redis_port, decode_responses=True
    )
    r.ping()
    print(f"[lseg-adapter] Redis connected at {args.redis_host}:{args.redis_port}")

    client = LSEGDataClient()
    rics = list(INSTRUMENTS.values())
    print(f"[lseg-adapter] polling {len(rics)} RICs every {args.interval}s: {rics}")

    seq = 0
    try:
        while True:
            ts = int(time.time() * 1000)

            try:
                # Fetch latest snapshot — use get_data for real-time fields
                df = client.get_data(
                    rics=rics,
                    fields=["TRDPRC_1", "SETTLE", "HIGH_1", "LOW_1"],
                )

                if df is not None and not df.empty:
                    for code, ric in INSTRUMENTS.items():
                        # Filter rows for this RIC
                        if ric in df.index:
                            row = df.loc[ric]
                        else:
                            continue

                        price = row.get("TRDPRC_1") or row.get("SETTLE")
                        if price is None:
                            continue

                        stream_key = f"market:futures:{code}"
                        r.xadd(
                            stream_key,
                            {"price": str(float(price)), "timestamp": str(ts)},
                            maxlen=MAXLEN,
                            approximate=True,
                        )

                    seq += 1
                    if seq % 5 == 0:
                        print(f"[lseg-adapter] published {seq} snapshots")

            except Exception as e:
                print(f"[lseg-adapter] fetch error: {e}", file=sys.stderr)

            # Heartbeat
            r.xadd(
                "heartbeat:lseg-adapter",
                {"status": "ok", "timestamp": str(ts)},
                maxlen=1000,
                approximate=True,
            )

            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n[lseg-adapter] stopped.")


if __name__ == "__main__":
    main()
