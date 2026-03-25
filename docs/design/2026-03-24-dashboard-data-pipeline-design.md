# Dashboard & Data Pipeline — Design Spec

**Date:** 2026-03-24
**Status:** Approved

## Overview

A live dashboard showing both raw market data (LSEG/BBG) and fin-kit calculated analytics as first-class citizens. fin-kit runs as a persistent service alongside the data feed. The system supports sub-second latency, local single-user deployment (with potential for shared use later), and historical data access via TimescaleDB.

## Architecture

Four independent processes communicate through Redis Streams:

```
┌─────────────────┐     ┌──────────────────┐
│  LSEG Ingestion │     │  fin-kit service  │
│  (Python)       │     │  (C++)            │
│                 │     │                   │
│  Connects to    │     │  Reads raw streams│
│  LSEG feed,     │────▶│  Runs calculations│
│  writes to Redis│     │  Writes result    │
│  Streams        │     │  streams          │
└────────┬────────┘     └────────┬──────────┘
         │                       │
         ▼                       ▼
┌──────────────────────────────────────────┐
│              Redis Streams               │
│                                          │
│  market:fx:*    market:rates:*   ...     │
│  calc:fed_probs  calc:basis  calc:curves │
└──────────────────┬───────────────────────┘
                   │
                   ▼
┌──────────────────────────────────────────┐
│          Bun WebSocket Server            │
│                                          │
│  Subscribes to all streams (raw + calc)  │
│  Pushes to browser via WebSocket         │
│  REST API for historical TimescaleDB     │
│  queries                                 │
└──────────────────┬───────────────────────┘
                   │
                   ▼
┌──────────────────────────────────────────┐
│         React Dashboard (Browser)        │
│                                          │
│  WebSocket for live data                 │
│  REST for historical queries             │
│  Lightweight Charts + custom panels      │
└──────────────────────────────────────────┘
```

### Design Principles

- **Redis is the single integration point.** No process talks directly to another.
- **Each process is independent.** Start/stop in any order; consumer groups handle reconnection and replay.
- **The Bun server is a thin relay.** It does not transform data — it fans out stream messages to connected browsers and proxies historical queries to TimescaleDB.
- **Raw and calculated data are first-class citizens.** The dashboard treats them identically.

## Technology Stack

| Component | Technology | Notes |
|-----------|-----------|-------|
| Message backbone | Redis Streams | Persistent messages, consumer groups, sub-millisecond latency |
| Market data ingestion | Python | Form TBD: submodule or thin adapter to existing LSEG tooling |
| Calculation service | C++ (fin-kit) | Persistent service linking existing fin-kit modules via redis-plus-plus |
| WebSocket backend | Bun (TypeScript) | Relay + REST API for historical TimescaleDB queries |
| Frontend | React + Lightweight Charts | PoC; may evolve to custom D3/Canvas for specialized panels |
| Historical storage | TimescaleDB (PostgreSQL) | Already in place |
| C++ Redis client | redis-plus-plus (Conan: `redis-plus-plus/1.3.15`) | C++20 support, wraps hiredis |

### Why Redis Streams over alternatives

- **vs Redis pub/sub:** Pub/sub is fire-and-forget — missed messages on restart. Streams persist and support replay.
- **vs Kafka:** Industry standard but heavy for local single-user (JVM, KRaft, ~1GB+ RAM). Graduate to Kafka if/when deployment becomes shared.

## Redis Streams Schema

### Raw market data (written by Python ingestion)

```
market:fx:{pair}          → bid, ask, mid, timestamp
market:rates:sofr         → rate, timestamp
market:rates:ois:{tenor}  → rate, timestamp
market:bonds:{cusip}      → price, yield, timestamp
market:futures:{code}     → price, timestamp
```

### Calculated data (written by fin-kit)

```
calc:fed_probs:{meeting}  → lower_move_bps, prob_lower, upper_move_bps, prob_upper, timestamp
calc:basis:bond:{cusip}   → gross_basis, net_basis, implied_repo, timestamp
calc:basis:cip:{pair}     → cip_basis_bps, timestamp
calc:curves:sofr          → JSON array of {tenor, rate} points, timestamp
```

### System streams

```
heartbeat:{service_name}  → status, timestamp
```

### TimescaleDB persistence

The **Bun server** is responsible for writing stream data to TimescaleDB for historical persistence. As it reads from Redis Streams for WebSocket fan-out, it also writes to the corresponding TimescaleDB tables. This keeps persistence in one place and avoids dual-write complexity in fin-kit or the Python ingestion layer.

Stream-to-table mapping follows the existing TimescaleDB schema:

| Redis Stream | TimescaleDB Table |
|---|---|
| `market:fx:{pair}` | `fx_spot` |
| `market:rates:sofr` | `rates_sofr_fixings` |
| `market:rates:ois:{tenor}` | `rates_ois_quotes` |
| `market:bonds:{cusip}` | `bonds_prices` |
| `market:futures:{code}` | `futures_treasury` |
| `calc:fed_probs:{meeting}` | `calculated_fed_probs` (new) |
| `calc:basis:bond:{cusip}` | `calculated_basis` |
| `calc:basis:cip:{pair}` | `calculated_basis` |
| `calc:curves:sofr` | `calculated_curves` |

The REST historical API (`GET /api/history/:stream`) translates stream names to the corresponding table queries using this mapping.

### Stream management

- `MAXLEN ~10000` per stream (approximate trimming, keeps memory bounded)
- Anything older than the stream buffer is in TimescaleDB
- Consumer groups: `finkit`, `dashboard` — each with a single consumer (single-user deployment). Adding consumers within a group splits messages rather than duplicating them — keep one consumer per group unless intentionally load-balancing.
- Redis persistence: AOF recommended for local dev to support replay-on-restart semantics
- Exact field schemas will evolve; the naming convention is the stable contract

## Communication Protocols

### WebSocket (Bun server → Browser)

```json
{
  "stream": "market:fx:eurusd",
  "data": { "bid": 1.0842, "ask": 1.0843, "mid": 1.08425 },
  "timestamp": 1711324800123
}
```

Same shape for raw and calculated data. The `stream` field routes to the appropriate panel.

### Subscription (Browser → Bun server)

```json
{ "subscribe": ["market:fx:*", "calc:fed_probs:*", "heartbeat:*"] }
```

Glob patterns allow panels to subscribe to categories without knowing every instrument upfront. Pattern matching is performed by the Bun server (Redis `XREAD` does not support globs natively) — the server maintains a list of active streams and filters on the client's behalf.

### REST API (historical data)

```
GET /api/history/:stream?from=<ts>&to=<ts>&limit=1000
```

Returns an array of the same `{ stream, data, timestamp }` shape. Panels seamlessly blend historical (REST) and live (WebSocket) data.

### Reconnection

- Browser reconnects WebSocket on disconnect, requests messages since last seen timestamp
- Bun server uses Redis consumer group — resumes from last acknowledged position on restart
- fin-kit same — consumer group handles replay

## Heartbeat & Calculation Timing

### Heartbeats

Each process writes to `heartbeat:{service_name}` on a regular interval. The Bun server monitors these and surfaces connection status on the dashboard (green/yellow/red). If a heartbeat goes stale, the dashboard shows it.

### Calculation windowing

fin-kit uses **time-windowed batching** (configurable, default 100ms). Incoming market data is buffered; when the window closes, calculations run against whatever arrived.

**Staleness constraint:** If any calculation input is consistently >1-2 seconds stale, escalate that calculation group to **dependency-based triggering** (fire only when all required inputs have updated within a tolerance). This is a localized change inside fin-kit and does not affect the rest of the architecture.

## Project Structure

```
fin-kit/
├── src/                    # C++ modules (existing)
├── apps/                   # C++ applications (existing)
├── tests/                  # C++ tests (existing)
├── docs/                   # Documentation (existing)
├── conanfile.py            # C++ deps (existing, add redis-plus-plus)
├── CMakeLists.txt          # C++ build (existing)
│
├── services/
│   └── finkit-stream/      # fin-kit as a persistent streaming service
│       ├── main.cpp        # Entry point — subscribe to Redis, run calc loop
│       └── CMakeLists.txt  # Links against fin-kit modules + redis-plus-plus
│
├── adapters/
│   └── lseg/               # LSEG ingestion adapter (submodule or wrapper, TBD)
│       ├── ingest.py
│       └── requirements.txt
│
├── web/
│   ├── server/             # Bun WebSocket backend
│   │   ├── src/
│   │   │   ├── index.ts    # Entry point
│   │   │   ├── redis.ts    # Redis Streams consumer
│   │   │   ├── ws.ts       # WebSocket fan-out
│   │   │   └── api.ts      # REST routes (historical queries)
│   │   ├── package.json
│   │   └── tsconfig.json
│   │
│   └── dashboard/          # React frontend
│       ├── src/
│       │   ├── App.tsx
│       │   ├── components/ # Panel components
│       │   └── hooks/      # WebSocket + data hooks
│       ├── package.json
│       └── tsconfig.json
│
├── .env                    # Local dev environment variables
└── docker-compose.yml      # Redis (+ optionally TimescaleDB for local dev)
```

### Key structural decisions

- `services/finkit-stream/` is a separate C++ binary linking against existing fin-kit modules. No duplication of calculation code. Lives under `services/` (not `apps/`) to distinguish long-running daemons from CLI tools.
- `web/server` and `web/dashboard` are separate packages. Bun can serve built React assets in production, but separate during dev.
- `docker-compose.yml` at root for spinning up Redis at minimum.
- `adapters/lseg/` is a placeholder — final form (submodule, wrapper, or inline) TBD.

## Configuration

All configuration via environment variables, consistent with existing TimescaleDB credential handling:

```bash
# Redis
REDIS_HOST=localhost
REDIS_PORT=6379

# TimescaleDB (existing, TSDB_* is primary convention)
TSDB_HOST=localhost
TSDB_PORT=5432
TSDB_DATABASE=finkit
TSDB_USER=           # from env
TSDB_PASSWORD=       # from env

# fin-kit stream service
FINKIT_STREAM_CALC_WINDOW_MS=100

# Web
WEB_PORT=3000
WEB_WS_PORT=3001
```

A `.env` file at the repo root for local dev, loaded by each process.

## Startup & Shutdown

Convenience scripts (`start.sh` / `stop.sh` or Makefile targets) to be designed once the architecture is built and the actual startup sequence is understood. For the PoC, manual startup is acceptable:

```bash
docker-compose up -d          # Redis
python adapters/lseg/ingest.py &
./build/services/finkit-stream &
cd web/server && bun run src/index.ts &
cd web/dashboard && bun run dev &
```

## Known Risks

- **redis-plus-plus with C++20 modules:** `services/finkit-stream/` will mix `#include` (redis-plus-plus headers) with `import` (fin-kit modules). This works in CMake 3.28+ with Clang but can be finicky — verify early.
- **Security:** No authentication on Redis or WebSocket server. Acceptable for localhost single-user. Must be addressed before any shared deployment.

## Deferred

- Dashboard panel design and frontend layout
- LSEG adapter packaging (submodule vs wrapper)
- Exact stream field schemas (will evolve with implementation)
- Graduation to Kafka if deployment becomes shared
- Testing strategy for streaming components
- Latency/throughput monitoring beyond heartbeats
- Mock ingestion script for testing without live LSEG feed
