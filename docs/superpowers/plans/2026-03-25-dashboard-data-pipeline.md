# Dashboard & Data Pipeline — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the end-to-end real-time dashboard pipeline: mock/LSEG market data → Redis Streams → C++ fin-kit calculations → Bun WebSocket relay → React dashboard rendering live US Treasury data.

**Architecture:** Six processes communicate exclusively through Redis Streams. A Python adapter (backed by jl-lseg-toolkit) ingests LSEG market data; a C++ fin-kit streaming service reads raw streams and writes calculated results; a Bun TypeScript server relays all streams to the browser via WebSocket and persists to TimescaleDB; a React dashboard renders live and historical data. A mock publisher enables development without a live LSEG feed. Each process is independent — start/stop in any order.

**Tech Stack:** Redis Streams, C++20 modules, redis-plus-plus 1.3.15 (Conan), Bun (TypeScript), tRPC + Valibot + TanStack Query, React 19, Vite, Lightweight Charts, ioredis, postgres (postgres.js), Python 3.12+ (jl-lseg-toolkit)

**Spec:** `docs/design/2026-03-24-dashboard-data-pipeline-design.md`

**Deviations from spec:**
- The spec lists separate `WEB_PORT` and `WEB_WS_PORT`. This plan uses a single Bun server port for both HTTP and WebSocket (upgrade on `/ws`), which is Bun's natural pattern. `WEB_WS_PORT` is removed from `.env.example`.
- The spec's stream-to-table mapping uses typed TimescaleDB tables. This PoC uses a `stream_log` catch-all JSONB table for persistence flexibility. Typed inserts are a follow-up.
- The C++ service writes `calc:implied_rate:{code}` streams (PoC calculation), which are not in the spec's stream schema. More complex calculations (bond basis, curves, fed probs) are a follow-up.
- Consumer group offsets differ intentionally: C++ service uses `$` (new messages only — live calculation), Bun server uses `0` (replay from start — ensures no gaps in persistence and client catch-up).

---


## Status Update — 2026-07-24

> **Snippet staleness:** the embedded code snippets below reflect the state at
> the time each step was written. The code is the source of truth; a
> multi-agent review pass (2026-07-24) hardened several components beyond what
> the snippets show. Key semantic differences from the snippets:
> - `persist.ts`: `initPersistence()` now takes a `PersistenceConfig` param
>   (env reading moved to `index.ts`) and is non-fatal on DB outage;
>   `STREAM_TABLE_MAP` includes `"calc:implied_rate:"`.
> - `index.ts`/`redis.ts`: `persistMessage` and `onMessage` are awaited
>   **before** XACK (at-least-once depends on this); consumer groups are
>   created **before** a stream is advertised to the consumer loop (NOGROUP
>   race).
> - `trpc.ts`: `limit` has `minValue(1)`; LIKE patterns escape `\`, `_`, `%`
>   via `streamPatternToSqlLike()`; `from`/`to` use `!== undefined`; history
>   rows return event time (embedded `timestamp`) via `normalizeHistoryData()`.
> - `useStream.ts`: reconnect lifecycle uses a per-effect `stopped` guard (no
>   zombie sockets on unmount/deps change).
> - `StatusBar.tsx`: heartbeat thresholds are green<10s / yellow<30s.
> - `main.cpp` (finkit-stream): drains its PEL on startup, recreates groups on
>   NOGROUP, withholds ACKs when a calc window fails, guards ACK/heartbeat
>   against transient Redis errors.
> - `ingest.py` (LSEG): looks up RICs in the `Instrument` column, skips NaN
>   prices, heartbeats only on successful fetch.

**Current branch status:** implemented on `feature/dashboard-data-pipeline` and validated locally in the `dashboard` worktree.

**Completed since the original plan was written:**
- Custom QuantLib + Conan/CMake build validated with the repo's intended LLVM/libc++ setup.
- Redis local-dev flow validated via Docker Desktop / WSL.
- Mock publisher → Redis → `finkit-stream` → Bun WebSocket relay → React dashboard live path validated end-to-end.
- TimescaleDB/Infisical-backed Bun persistence path validated against the shared TSDB.
- Post-plan fixes landed for dashboard history/persistence:
  - `fix: persist implied-rate history in dashboard server`
  - `fix: tighten dashboard history query validation`
- Historical preload + stitch-to-live: charts now seed from
  `trpc.history.byStream` (`InstrumentPanel` in `App.tsx`) and merge with live
  WebSocket updates in `TreasuryChart`.
- Multi-agent code-review pass (2026-07-24) fixed crash/data-loss/lifecycle
  defects across `finkit-stream`, the Bun server, the dashboard, and the LSEG
  adapter (see snippet-staleness note above).
- Repo-local TSDB/Infisical skill added:
  - `skills/timescaledb-access/SKILL.md`
  - `.claude/agents/timescaledb-access.md`
- Setup / local-service / onboarding docs were swept and clarified on this branch.

**Not yet completed:**
- Manual LSEG Workspace validation remains outstanding.
- Persistence is still the PoC `stream_log` catch-all rather than typed per-table writes.

**Resume here next session:**
1. Add an integration test covering Redis → Bun persistence → `history.byStream` ([#15](https://github.com/jlipworth/fin-kit/issues/15)).
2. Perform the manual LSEG Workspace validation when credentials/workspace are available ([#14](https://github.com/jlipworth/fin-kit/issues/14)).

Follow-up work from this plan is tracked in GitHub issues (see also
[#1](https://github.com/jlipworth/fin-kit/issues/1) typed persistence,
[#2](https://github.com/jlipworth/fin-kit/issues/2) real calculations,
[#3](https://github.com/jlipworth/fin-kit/issues/3) additional panels).

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `docker-compose.yml` | Create | Redis service for local dev |
| `.env.example` | Create | Template for environment variables |
| `.gitignore` | Modify | Add node_modules, .env, web build artifacts |
| `conanfile.py` | Modify | Add `redis-plus-plus/1.3.15` |
| `CMakeLists.txt` | Modify | Add `find_package(redis++)`, add `services/` subdirectory |
| `services/CMakeLists.txt` | Create | Services subdirectory build config |
| `services/finkit-stream/CMakeLists.txt` | Create | Streaming service build config |
| `services/finkit-stream/main.cpp` | Create | Entry point: Redis consumer, calc loop, heartbeat |
| `adapters/mock/publish_mock_data.py` | Create | Mock US Treasury data → Redis Streams |
| `adapters/mock/pyproject.toml` | Create | Python project config (redis dep) |
| `adapters/lseg/ingest.py` | Create | LSEG → Redis Streams adapter |
| `adapters/lseg/pyproject.toml` | Create | Python project config (redis dep) |
| `.gitmodules` | Create | Add jl-lseg-toolkit submodule |
| `web/server/package.json` | Create | Bun server deps |
| `web/server/tsconfig.json` | Create | TypeScript config |
| `web/server/src/index.ts` | Create | Entry point: HTTP + WebSocket server |
| `web/server/src/redis.ts` | Create | Redis Streams consumer + stream discovery |
| `web/server/src/ws.ts` | Create | WebSocket connection management + fan-out |
| `web/server/src/trpc.ts` | Create | tRPC router for historical TimescaleDB queries |
| `web/server/src/persist.ts` | Create | Stream → TimescaleDB persistence writer |
| `web/server/src/streams.test.ts` | Create | Tests for glob pattern matching |
| `web/dashboard/package.json` | Create | React dashboard deps |
| `web/dashboard/tsconfig.json` | Create | TypeScript config |
| `web/dashboard/vite.config.ts` | Create | Vite build config |
| `web/dashboard/index.html` | Create | HTML entry point |
| `web/dashboard/src/main.tsx` | Create | React entry point |
| `web/dashboard/src/App.tsx` | Create | Main app component + layout |
| `web/dashboard/src/App.css` | Create | Dashboard styles |
| `web/dashboard/src/lib/trpc.ts` | Create | tRPC client + React Query setup |
| `web/dashboard/src/hooks/useStream.ts` | Create | WebSocket subscription hook |
| `web/dashboard/src/components/TreasuryChart.tsx` | Create | Live price chart (Lightweight Charts) |
| `web/dashboard/src/components/StatusBar.tsx` | Create | Heartbeat / connection status |

---

## Chunk 1: Infrastructure & Build System

### Task 1: Docker Compose + environment template

**Files:**
- Create: `docker-compose.yml`
- Create: `.env.example`
- Modify: `.gitignore`

- [x] **Step 1: Create `docker-compose.yml`**

```yaml
services:
  redis:
    image: redis:7-alpine
    ports:
      - "${REDIS_PORT:-6379}:6379"
    command: redis-server --appendonly yes
    volumes:
      - redis_data:/data

volumes:
  redis_data:
```

AOF persistence enabled per spec for replay-on-restart semantics.

- [x] **Step 2: Create `.env.example`**

```bash
# Redis
REDIS_HOST=localhost
REDIS_PORT=6379

# fin-kit stream service
FINKIT_STREAM_CALC_WINDOW_MS=100

# Web (single port — Bun serves HTTP + WebSocket on the same port)
WEB_PORT=3000

# TimescaleDB — see docs/SETUP.md for TSDB_* vars
# Injected via: infisical run --env=dev --path="/kubernetes/infrastructure/timescaledb" -- <command>
```

- [x] **Step 3: Add entries to `.gitignore`**

Append to `.gitignore`:

```
# Dashboard
node_modules/
web/dashboard/dist/
.env
.venv
```

- [x] **Step 4: Start Redis and verify**

```bash
docker compose up -d
docker compose exec redis redis-cli ping
```

Expected: `PONG`

- [x] **Step 5: Commit**

```bash
git add docker-compose.yml .env.example .gitignore
git commit -m "chore: add docker-compose for Redis and .env.example"
```

---

### Task 2: Add redis-plus-plus to C++ build system

**Files:**
- Modify: `conanfile.py:15`
- Modify: `CMakeLists.txt`
- Create: `services/CMakeLists.txt`

- [x] **Step 1: Add redis-plus-plus to `conanfile.py`**

After the existing `self.requires("libpqxx/7.9.2")` line, add:

```python
        self.requires("redis-plus-plus/1.3.15")
```

- [x] **Step 2: Run Conan install to verify the package resolves**

Use the same compiler settings as existing builds:

```bash
CC=/opt/homebrew/opt/llvm/bin/clang \
CXX=/opt/homebrew/opt/llvm/bin/clang++ \
LDFLAGS="-L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++" \
uv run conan install . --build=missing -of=build \
  -s compiler=clang -s compiler.version=21 -s compiler.cppstd=20 -s compiler.libcxx=libc++
```

Expected: redis-plus-plus and hiredis (transitive) resolve and build successfully.

- [x] **Step 3: Verify the CMake package and target names**

```bash
grep -r "redis" build/ --include="*.cmake" | head -10
```

Expected: Shows the generated CMake config files for redis-plus-plus. Look for the exact `find_package` name (likely `redis++`) and target name (likely `redis++::redis++_static` or `redis++::redis++`). Use these exact names in the steps below.

- [x] **Step 4: Add `find_package` and `services/` subdirectory to root `CMakeLists.txt`**

After the existing `find_package(libpqxx REQUIRED)` line, add (using the package name from Step 3):

```cmake
find_package(redis++ REQUIRED)
```

After the existing `add_subdirectory(apps)` line, add:

```cmake
add_subdirectory(services)
```

- [x] **Step 5: Create `services/CMakeLists.txt`**

```cmake
add_subdirectory(finkit-stream)
```

- [x] **Step 6: Create minimal `services/finkit-stream/CMakeLists.txt` to verify linkage**

```cmake
add_executable(finkit-stream main.cpp)

target_link_libraries(finkit-stream PRIVATE
    finkit::core
    redis++::redis++_static
)

target_compile_features(finkit-stream PRIVATE cxx_std_20)
```

Note: Use the target name verified in Step 3. The target may be `redis++::redis++` instead of `redis++::redis++_static`. hiredis is linked transitively.

- [x] **Step 7: Create minimal `services/finkit-stream/main.cpp` to test compilation**

`main.cpp` must be a **regular translation unit** (not a module interface unit) because it mixes `#include` (redis-plus-plus headers) with `import` (fin-kit modules). The design spec flags this as a known risk — CMake 3.28+ with Clang handles this pattern when the file is NOT a module:

```cpp
#include <sw/redis++/redis++.h>
#include <iostream>

import finkit.core;

int main() {
    std::cout << "finkit-stream: build OK\n";
    return 0;
}
```

- [x] **Step 8: Build to verify redis-plus-plus + C++20 modules coexist**

```bash
cmake --preset release
cmake --build build/build/Release --target finkit-stream
./build/build/Release/services/finkit-stream/finkit-stream
```

Expected: Prints `finkit-stream: build OK`.

- [x] **Step 9: Commit**

```bash
git add conanfile.py CMakeLists.txt services/
git commit -m "feat: add redis-plus-plus dependency and services/ build scaffolding"
```

---

## Chunk 2: Mock Data Publisher

Enables testing the entire pipeline without a live LSEG feed. Publishes realistic US Treasury futures data to Redis Streams.

### Task 3: Mock treasury data publisher

**Files:**
- Create: `adapters/mock/publish_mock_data.py`
- Create: `adapters/mock/pyproject.toml`

- [x] **Step 1: Create `adapters/mock/pyproject.toml`**

```toml
[project]
name = "finkit-mock-publisher"
version = "0.1.0"
requires-python = ">=3.12"
dependencies = [
    "redis>=5.0",
]
```

- [x] **Step 2: Create `adapters/mock/publish_mock_data.py`**

```python
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
```

- [x] **Step 3: Install deps and run the publisher**

```bash
cd adapters/mock
uv sync
uv run publish_mock_data.py --interval 0.5
```

Expected: Prints price updates every 5 ticks. Leave running.

- [x] **Step 4: Verify streams exist in Redis**

In a separate terminal:

```bash
docker compose exec redis redis-cli XLEN market:futures:TY
docker compose exec redis redis-cli XRANGE market:futures:TY - + COUNT 3
```

Expected: `XLEN` returns a positive number. `XRANGE` shows entries with `price` and `timestamp` fields.

- [x] **Step 5: Commit**

```bash
git add adapters/mock/
git commit -m "feat: add mock treasury data publisher for Redis Streams

Uses uv for Python dependency management."
```

---

## Chunk 3: Bun WebSocket Server

The Bun server is a thin relay: reads all Redis Streams, fans out to WebSocket clients based on subscription patterns, and provides a REST API for historical TimescaleDB queries. It also persists stream data to TimescaleDB.

**Dependencies on prior chunks:** Chunk 1 (Redis running).

### Task 4: Server setup, Redis consumer, and WebSocket fan-out

**Files:**
- Create: `web/server/package.json`
- Create: `web/server/tsconfig.json`
- Create: `web/server/src/redis.ts`
- Create: `web/server/src/ws.ts`
- Create: `web/server/src/index.ts`
- Create: `web/server/src/streams.test.ts`

- [x] **Step 1: Write the glob pattern matching test**

Create `web/server/src/streams.test.ts`:

```typescript
import { describe, expect, test } from "bun:test";
import { matchesPattern } from "./ws";

describe("matchesPattern", () => {
  test("exact match", () => {
    expect(matchesPattern("market:futures:TY", "market:futures:TY")).toBe(true);
  });

  test("wildcard at end", () => {
    expect(matchesPattern("market:futures:TY", "market:futures:*")).toBe(true);
    expect(matchesPattern("market:futures:US", "market:futures:*")).toBe(true);
  });

  test("wildcard does not match different prefix", () => {
    expect(matchesPattern("calc:basis:bond:X", "market:futures:*")).toBe(false);
  });

  test("wildcard in middle", () => {
    expect(matchesPattern("market:rates:ois:1Y", "market:rates:*")).toBe(true);
  });

  test("heartbeat pattern", () => {
    expect(matchesPattern("heartbeat:finkit-stream", "heartbeat:*")).toBe(true);
    expect(matchesPattern("heartbeat:mock-publisher", "heartbeat:*")).toBe(true);
  });

  test("subscribe-all pattern", () => {
    expect(matchesPattern("market:futures:TY", "*")).toBe(true);
    expect(matchesPattern("calc:curves:sofr", "*")).toBe(true);
  });
});
```

- [x] **Step 2: Create `web/server/package.json`**

```json
{
  "name": "finkit-dashboard-server",
  "version": "0.1.0",
  "private": true,
  "scripts": {
    "dev": "bun run --watch src/index.ts",
    "start": "bun run src/index.ts",
    "test": "bun test"
  },
  "dependencies": {
    "@trpc/server": "^11.0.0",
    "ioredis": "^5.4.1",
    "postgres": "^3.4.5",
    "valibot": "^1.0.0"
  }
}
```

- [x] **Step 3: Create `web/server/tsconfig.json`**

```json
{
  "compilerOptions": {
    "target": "ESNext",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "strict": true,
    "skipLibCheck": true,
    "types": ["bun-types"]
  },
  "include": ["src"]
}
```

- [x] **Step 4: Install dependencies**

```bash
cd web/server
bun install
```

- [x] **Step 5: Create `web/server/src/ws.ts`**

```typescript
import type { ServerWebSocket } from "bun";

/** Per-connection state attached via ws.data */
export interface WsData {
  patterns: string[];
  lastId: string;
}

/** All active WebSocket connections */
const clients = new Set<ServerWebSocket<WsData>>();

/**
 * Test whether a Redis stream name matches a subscription glob pattern.
 * Supports * as a wildcard for any suffix.
 */
export function matchesPattern(stream: string, pattern: string): boolean {
  if (pattern === "*") return true;
  if (!pattern.includes("*")) return stream === pattern;
  const prefix = pattern.slice(0, pattern.indexOf("*"));
  return stream.startsWith(prefix);
}

/** Register a new WebSocket connection */
export function addClient(ws: ServerWebSocket<WsData>) {
  clients.add(ws);
}

/** Remove a disconnected WebSocket */
export function removeClient(ws: ServerWebSocket<WsData>) {
  clients.delete(ws);
}

/** Handle incoming subscription message from a client */
export function handleSubscribe(ws: ServerWebSocket<WsData>, msg: string) {
  try {
    const parsed = JSON.parse(msg);
    if (Array.isArray(parsed.subscribe)) {
      ws.data.patterns = parsed.subscribe;
    }
  } catch {
    // Ignore malformed messages
  }
}

/**
 * Fan out a stream message to all clients whose subscription patterns match.
 * Message shape follows the design spec: { stream, data, timestamp }
 */
export function fanOut(stream: string, data: Record<string, string>) {
  const timestamp = data.timestamp || String(Date.now());
  const { timestamp: _, ...fields } = data;
  const msg = JSON.stringify({ stream, data: fields, timestamp: Number(timestamp) });

  for (const ws of clients) {
    if (ws.data.patterns.length === 0) continue;
    if (ws.data.patterns.some((p) => matchesPattern(stream, p))) {
      ws.send(msg);
    }
  }
}

/** Bun WebSocket handler config */
export const websocketHandler = {
  open(ws: ServerWebSocket<WsData>) {
    addClient(ws);
    console.log(`[ws] client connected (${clients.size} total)`);
  },
  message(ws: ServerWebSocket<WsData>, msg: string | Buffer) {
    handleSubscribe(ws, typeof msg === "string" ? msg : msg.toString());
  },
  close(ws: ServerWebSocket<WsData>) {
    removeClient(ws);
    console.log(`[ws] client disconnected (${clients.size} total)`);
  },
};
```

- [x] **Step 6: Run the pattern matching test**

```bash
cd web/server
bun test
```

Expected: 6 tests pass.

- [x] **Step 7: Create `web/server/src/redis.ts`**

```typescript
import Redis from "ioredis";

const STREAM_PREFIXES = ["market:", "calc:", "heartbeat:"];
const CONSUMER_GROUP = "dashboard";
const CONSUMER_NAME = "dashboard-1";
const SCAN_INTERVAL_MS = 5_000;

let redis: Redis;
let subscriberRedis: Redis; // separate connection for blocking XREADGROUP

/** Known streams (discovered via periodic SCAN) */
const knownStreams = new Set<string>();

export function getRedis(): Redis {
  return redis;
}

/** Initialize Redis connections */
export function initRedis(host: string, port: number) {
  redis = new Redis({ host, port, maxRetriesPerRequest: null });
  subscriberRedis = new Redis({ host, port, maxRetriesPerRequest: null });
}

/** Discover streams by scanning for keys matching our prefixes */
async function discoverStreams() {
  for (const prefix of STREAM_PREFIXES) {
    let cursor = "0";
    do {
      const [next, keys] = await redis.scan(cursor, "MATCH", `${prefix}*`, "COUNT", 100);
      cursor = next;
      for (const key of keys) {
        if (!knownStreams.has(key)) {
          knownStreams.add(key);
          // Create consumer group — ignore error if it already exists
          try {
            await redis.xgroup("CREATE", key, CONSUMER_GROUP, "0", "MKSTREAM");
          } catch {
            // BUSYGROUP — group already exists, fine
          }
          console.log(`[redis] discovered stream: ${key}`);
        }
      }
    } while (cursor !== "0");
  }
}

/**
 * Start the consumer loop.
 * Calls onMessage(stream, data) for every new message across all discovered streams.
 * Periodically scans for new streams.
 */
export async function startConsumer(
  onMessage: (stream: string, data: Record<string, string>) => void,
) {
  // Initial discovery
  await discoverStreams();

  // Periodic re-discovery
  setInterval(discoverStreams, SCAN_INTERVAL_MS);

  // Consumer loop
  while (true) {
    const streams = Array.from(knownStreams);
    if (streams.length === 0) {
      await new Promise((r) => setTimeout(r, 1000));
      continue;
    }

    try {
      const results = await subscriberRedis.xreadgroup(
        "GROUP", CONSUMER_GROUP, CONSUMER_NAME,
        "BLOCK", 1000,
        "COUNT", 100,
        "STREAMS", ...streams, ...streams.map(() => ">"),
      );

      if (results) {
        for (const [stream, messages] of results) {
          for (const [id, fields] of messages) {
            // fields is [key, value, key, value, ...]
            const data: Record<string, string> = {};
            for (let i = 0; i < fields.length; i += 2) {
              data[fields[i]] = fields[i + 1];
            }
            onMessage(stream, data);
            await redis.xack(stream, CONSUMER_GROUP, id);
          }
        }
      }
    } catch (err) {
      // Connection error — wait and retry
      console.error("[redis] consumer error:", err);
      await new Promise((r) => setTimeout(r, 2000));
    }
  }
}
```

- [x] **Step 8: Create `web/server/src/index.ts`**

```typescript
import { initRedis, startConsumer } from "./redis";
import { websocketHandler, fanOut, type WsData } from "./ws";

const WEB_PORT = parseInt(process.env.WEB_PORT || "3000");
const REDIS_HOST = process.env.REDIS_HOST || "localhost";
const REDIS_PORT = parseInt(process.env.REDIS_PORT || "6379");

initRedis(REDIS_HOST, REDIS_PORT);

const server = Bun.serve<WsData>({
  port: WEB_PORT,

  fetch(req, server) {
    const url = new URL(req.url);

    // WebSocket upgrade
    if (url.pathname === "/ws") {
      const upgraded = server.upgrade(req, {
        data: { patterns: [], lastId: "0" },
      });
      if (upgraded) return undefined;
      return new Response("WebSocket upgrade failed", { status: 500 });
    }

    // Health check
    if (url.pathname === "/health") {
      return Response.json({ status: "ok" });
    }

    // REST API placeholder (Task 5 adds full historical API)
    if (url.pathname.startsWith("/api/")) {
      return Response.json({ error: "not implemented" }, { status: 501 });
    }

    return new Response("Not found", { status: 404 });
  },

  websocket: websocketHandler,
});

// Start Redis consumer → fan out to WebSocket clients
startConsumer((stream, data) => {
  fanOut(stream, data);
});

console.log(`[server] listening on http://localhost:${server.port}`);
console.log(`[server] WebSocket at ws://localhost:${server.port}/ws`);
```

- [x] **Step 9: Verify end-to-end with mock publisher**

Terminal 1 — mock publisher (if not already running):
```bash
cd adapters/mock && uv run publish_mock_data.py --interval 0.5
```

Terminal 2 — Bun server:
```bash
cd web/server && bun run dev
```

Terminal 3 — test WebSocket with `websocat` or a quick script:
```bash
echo '{"subscribe":["market:futures:*","heartbeat:*"]}' | websocat ws://localhost:3000/ws
```

Expected: JSON messages stream in with shape `{"stream":"market:futures:TY","data":{"price":"110.75"},"timestamp":1711324800123}`.

If `websocat` is not installed, use a quick Node/Bun one-liner:
```bash
bun -e "const ws = new WebSocket('ws://localhost:3000/ws'); ws.onopen = () => ws.send(JSON.stringify({subscribe:['market:futures:*','heartbeat:*']})); ws.onmessage = e => console.log(e.data);"
```

- [x] **Step 10: Commit**

```bash
git add web/server/
git commit -m "feat: add Bun WebSocket server with Redis Streams consumer and fan-out"
```

---

### Task 5: tRPC router, TimescaleDB persistence, and historical queries

**Files:**
- Create: `web/server/src/persist.ts`
- Create: `web/server/src/trpc.ts`
- Modify: `web/server/src/index.ts`

- [x] **Step 1: Create `web/server/src/persist.ts`**

```typescript
import postgres from "postgres";

/**
 * Stream name → TimescaleDB table mapping, per design spec.
 */
const STREAM_TABLE_MAP: Record<string, string> = {
  "market:fx:": "fx_spot",
  "market:rates:sofr": "rates_sofr_fixings",
  "market:rates:ois:": "rates_ois_quotes",
  "market:bonds:": "bonds_prices",
  "market:futures:": "futures_treasury",
  "calc:fed_probs:": "calculated_fed_probs",
  "calc:basis:bond:": "calculated_basis",
  "calc:basis:cip:": "calculated_basis",
  "calc:curves:sofr": "calculated_curves",
};

let sql: ReturnType<typeof postgres> | null = null;

/** Initialize TimescaleDB connection and ensure schema. Skips if TSDB_HOST is not set. */
export async function initPersistence() {
  const host = process.env.TSDB_HOST || process.env.POSTGRES_HOST;
  if (!host) {
    console.log("[persist] TSDB_HOST not set — persistence disabled");
    return;
  }

  sql = postgres({
    host,
    port: parseInt(process.env.TSDB_PORT || process.env.POSTGRES_PORT || "5432"),
    database: process.env.TSDB_DATABASE || process.env.POSTGRES_DB || "timeseries",
    username: process.env.TSDB_USER || process.env.POSTGRES_USER || "postgres",
    password: process.env.TSDB_PASSWORD || process.env.POSTGRES_PASSWORD || "",
  });

  // Create the catch-all stream log table if it doesn't exist.
  // This is a PoC approach — typed per-table inserts are a follow-up.
  await sql`
    CREATE TABLE IF NOT EXISTS stream_log (
      id BIGINT GENERATED ALWAYS AS IDENTITY,
      stream TEXT NOT NULL,
      table_name TEXT NOT NULL,
      data JSONB NOT NULL,
      ts TIMESTAMPTZ NOT NULL DEFAULT NOW()
    )
  `;
  await sql`
    CREATE INDEX IF NOT EXISTS idx_stream_log_stream_ts ON stream_log (stream, ts DESC)
  `;

  console.log(`[persist] connected to TimescaleDB at ${host}`);
}

/** Resolve a stream name to its TimescaleDB table */
export function resolveTable(stream: string): string | null {
  for (const [prefix, table] of Object.entries(STREAM_TABLE_MAP)) {
    if (stream === prefix || stream.startsWith(prefix)) return table;
  }
  return null;
}

/** Persist a stream message to TimescaleDB. Best-effort — logs errors, does not throw. */
export async function persistMessage(stream: string, data: Record<string, string>) {
  if (!sql) return;

  const table = resolveTable(stream);
  if (!table) return;

  try {
    await sql`
      INSERT INTO stream_log (stream, table_name, data, ts)
      VALUES (${stream}, ${table}, ${JSON.stringify(data)}, NOW())
    `;
  } catch (err) {
    console.error(`[persist] error writing to ${table}:`, err);
  }
}

/** Get postgres connection for historical queries */
export function getSql() {
  return sql;
}
```

- [x] **Step 2: Create `web/server/src/trpc.ts`**

```typescript
import { initTRPC, TRPCError } from "@trpc/server";
import * as v from "valibot";
import { getSql } from "./persist";

const t = initTRPC.create();

export const appRouter = t.router({
  history: t.router({
    /**
     * Fetch historical stream data from TimescaleDB.
     * Replaces: GET /api/history/:stream?from=<ts>&to=<ts>&limit=1000
     * Returns same { stream, data, timestamp } shape as WebSocket messages.
     */
    byStream: t.procedure
      .input(
        v.parser(
          v.object({
            stream: v.string(),
            from: v.optional(v.number()),
            to: v.optional(v.number()),
            limit: v.optional(v.pipe(v.number(), v.maxValue(10_000))),
          }),
        ),
      )
      .query(async ({ input }) => {
        const sql = getSql();
        if (!sql) {
          throw new TRPCError({
            code: "PRECONDITION_FAILED",
            message: "TimescaleDB not configured",
          });
        }

        const limit = input.limit ?? 1000;

        const rows = await sql`
          SELECT stream, data, ts
          FROM stream_log
          WHERE stream LIKE ${input.stream.replace("*", "%")}
            ${input.from ? sql`AND ts >= to_timestamp(${input.from} / 1000.0)` : sql``}
            ${input.to ? sql`AND ts <= to_timestamp(${input.to} / 1000.0)` : sql``}
          ORDER BY ts DESC
          LIMIT ${limit}
        `;

        return rows.map((r: any) => ({
          stream: r.stream as string,
          data: r.data as Record<string, string>,
          timestamp: new Date(r.ts).getTime(),
        }));
      }),
  }),
});

/** Export the router type for the dashboard client */
export type AppRouter = typeof appRouter;
```

- [x] **Step 3: Wire tRPC and persistence into `index.ts`**

In `web/server/src/index.ts`, add imports and initialization.

After the existing `import` lines, add:

```typescript
import { fetchRequestHandler } from "@trpc/server/adapters/fetch";
import { appRouter } from "./trpc";
import { initPersistence, persistMessage } from "./persist";
```

After `initRedis(REDIS_HOST, REDIS_PORT);`, add:

```typescript
await initPersistence();
```

Replace the `/api/` handler block:

```typescript
    // tRPC API
    if (url.pathname.startsWith("/api/trpc")) {
      return fetchRequestHandler({
        endpoint: "/api/trpc",
        req,
        router: appRouter,
        createContext: () => ({}),
      });
    }
```

Replace the `startConsumer` callback:

```typescript
startConsumer((stream, data) => {
  fanOut(stream, data);
  persistMessage(stream, data);
});
```

- [x] **Step 4: Verify the server still starts cleanly**

```bash
cd web/server && bun run dev
```

Expected: Server starts. Logs `[persist] TSDB_HOST not set — persistence disabled` (acceptable for local dev without TimescaleDB). WebSocket fan-out still works as before.

- [x] **Step 5: Commit**

```bash
git add web/server/src/trpc.ts web/server/src/persist.ts web/server/src/index.ts
git commit -m "feat: add tRPC router and TimescaleDB persistence to Bun server"
```

---

## Chunk 4: React Dashboard

Minimal working dashboard that renders live US Treasury futures prices and connection status.

**Dependencies on prior chunks:** Chunk 1 (Redis), Chunk 3 (Bun server).

### Task 6: Project setup, WebSocket hook, and treasury chart

**Files:**
- Create: `web/dashboard/package.json`
- Create: `web/dashboard/tsconfig.json`
- Create: `web/dashboard/vite.config.ts`
- Create: `web/dashboard/index.html`
- Create: `web/dashboard/src/main.tsx`
- Create: `web/dashboard/src/lib/trpc.ts`
- Create: `web/dashboard/src/App.tsx`
- Create: `web/dashboard/src/App.css`
- Create: `web/dashboard/src/hooks/useStream.ts`
- Create: `web/dashboard/src/components/TreasuryChart.tsx`

- [x] **Step 1: Create `web/dashboard/package.json`**

```json
{
  "name": "finkit-dashboard",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "vite build",
    "preview": "vite preview"
  },
  "dependencies": {
    "@tanstack/react-query": "^5.62.0",
    "@trpc/client": "^11.0.0",
    "@trpc/react-query": "^11.0.0",
    "lightweight-charts": "^4.2.1",
    "react": "^19.0.0",
    "react-dom": "^19.0.0"
  },
  "devDependencies": {
    "@types/react": "^19.0.0",
    "@types/react-dom": "^19.0.0",
    "@vitejs/plugin-react": "^4.3.4",
    "typescript": "^5.7.0",
    "vite": "^6.0.0"
  }
}
```

- [x] **Step 2: Create `web/dashboard/tsconfig.json`**

```json
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "jsx": "react-jsx",
    "strict": true,
    "skipLibCheck": true
  },
  "include": ["src"]
}
```

- [x] **Step 3: Create `web/dashboard/vite.config.ts`**

```typescript
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      "/ws": {
        target: "ws://localhost:3000",
        ws: true,
      },
      "/api/trpc": {
        target: "http://localhost:3000",
      },
    },
  },
});
```

The Vite dev server proxies `/ws` and `/api` to the Bun server, avoiding CORS issues.

- [x] **Step 4: Create `web/dashboard/index.html`**

```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>fin-kit Dashboard</title>
  </head>
  <body>
    <div id="root"></div>
    <script type="module" src="/src/main.tsx"></script>
  </body>
</html>
```

- [x] **Step 5: Create `web/dashboard/src/lib/trpc.ts`**

This sets up the tRPC client and React Query integration. The `AppRouter` type is imported from the server to get end-to-end type safety.

```typescript
import { createTRPCReact } from "@trpc/react-query";
import { httpBatchLink } from "@trpc/client";
import type { AppRouter } from "../../../server/src/trpc";

export const trpc = createTRPCReact<AppRouter>();

export const trpcClient = trpc.createClient({
  links: [
    httpBatchLink({
      url: "/api/trpc",
    }),
  ],
});
```

The relative import `../../../server/src/trpc` works because both packages live under `web/`. Only the `AppRouter` type is imported — no runtime code crosses the boundary.

- [x] **Step 6: Create `web/dashboard/src/main.tsx`**

```tsx
import { useState } from "react";
import { createRoot } from "react-dom/client";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { trpc, trpcClient } from "./lib/trpc";
import App from "./App";
import "./App.css";

function Root() {
  const [queryClient] = useState(() => new QueryClient());

  return (
    <trpc.Provider client={trpcClient} queryClient={queryClient}>
      <QueryClientProvider client={queryClient}>
        <App />
      </QueryClientProvider>
    </trpc.Provider>
  );
}

createRoot(document.getElementById("root")!).render(<Root />);
```

- [x] **Step 7: Create `web/dashboard/src/hooks/useStream.ts`**

```typescript
import { useEffect, useRef, useState, useCallback } from "react";

interface StreamMessage {
  stream: string;
  data: Record<string, string>;
  timestamp: number;
}

interface UseStreamOptions {
  /** WebSocket URL (defaults to ws://localhost:3000/ws in dev) */
  url?: string;
  /** Glob patterns to subscribe to */
  patterns: string[];
}

interface UseStreamResult {
  /** Most recent message per stream */
  latest: Map<string, StreamMessage>;
  /** Whether the WebSocket is connected */
  connected: boolean;
}

/**
 * Hook that subscribes to Redis Streams via the Bun WebSocket server.
 * Returns the latest message per stream, updated in real-time.
 */
export function useStream({ url, patterns }: UseStreamOptions): UseStreamResult {
  const wsUrl = url || `ws://${window.location.host}/ws`;
  const wsRef = useRef<WebSocket | null>(null);
  const [connected, setConnected] = useState(false);
  const [latest, setLatest] = useState<Map<string, StreamMessage>>(new Map());

  const connect = useCallback(() => {
    const ws = new WebSocket(wsUrl);

    ws.onopen = () => {
      setConnected(true);
      ws.send(JSON.stringify({ subscribe: patterns }));
    };

    ws.onmessage = (event) => {
      try {
        const msg: StreamMessage = JSON.parse(event.data);
        setLatest((prev) => {
          const next = new Map(prev);
          next.set(msg.stream, msg);
          return next;
        });
      } catch {
        // Ignore malformed
      }
    };

    ws.onclose = () => {
      setConnected(false);
      // Reconnect after 2s
      setTimeout(connect, 2000);
    };

    ws.onerror = () => ws.close();

    wsRef.current = ws;
  }, [wsUrl, patterns]);

  useEffect(() => {
    connect();
    return () => wsRef.current?.close();
  }, [connect]);

  return { latest, connected };
}
```

- [x] **Step 8: Create `web/dashboard/src/components/TreasuryChart.tsx`**

```tsx
import { useEffect, useRef } from "react";
import { createChart, type IChartApi, type ISeriesApi, type LineData, type Time } from "lightweight-charts";

interface TreasuryChartProps {
  /** Stream name, e.g. "market:futures:TY" */
  stream: string;
  /** Latest data point */
  latest: { price: string; timestamp: number } | undefined;
  /** Display label */
  label: string;
}

export function TreasuryChart({ stream, latest, label }: TreasuryChartProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const chartRef = useRef<IChartApi | null>(null);
  const seriesRef = useRef<ISeriesApi<"Line"> | null>(null);

  // Create chart once
  useEffect(() => {
    if (!containerRef.current) return;

    const chart = createChart(containerRef.current, {
      width: containerRef.current.clientWidth,
      height: 250,
      layout: {
        background: { color: "#1a1a2e" },
        textColor: "#e0e0e0",
      },
      grid: {
        vertLines: { color: "#2a2a3e" },
        horzLines: { color: "#2a2a3e" },
      },
      timeScale: { timeVisible: true, secondsVisible: true },
    });

    const series = chart.addLineSeries({
      color: "#4fc3f7",
      lineWidth: 2,
    });

    chartRef.current = chart;
    seriesRef.current = series;

    const onResize = () => {
      if (containerRef.current) {
        chart.applyOptions({ width: containerRef.current.clientWidth });
      }
    };
    window.addEventListener("resize", onResize);

    return () => {
      window.removeEventListener("resize", onResize);
      chart.remove();
    };
  }, []);

  // Update chart with new data
  useEffect(() => {
    if (!latest || !seriesRef.current) return;
    const point: LineData<Time> = {
      time: (latest.timestamp / 1000) as Time,
      value: parseFloat(latest.price),
    };
    seriesRef.current.update(point);
  }, [latest]);

  return (
    <div className="chart-panel">
      <div className="chart-header">
        <span className="chart-label">{label}</span>
        {latest && (
          <span className="chart-price">{parseFloat(latest.price).toFixed(4)}</span>
        )}
      </div>
      <div ref={containerRef} />
    </div>
  );
}
```

- [x] **Step 9: Create `web/dashboard/src/App.tsx`**

```tsx
import { useMemo } from "react";
import { useStream } from "./hooks/useStream";
import { TreasuryChart } from "./components/TreasuryChart";
import { StatusBar } from "./components/StatusBar";

const TREASURY_INSTRUMENTS = [
  { code: "TU", label: "2Y T-Note (TU)" },
  { code: "FV", label: "5Y T-Note (FV)" },
  { code: "TY", label: "10Y T-Note (TY)" },
  { code: "US", label: "30Y T-Bond (US)" },
];

const PATTERNS = ["market:futures:*", "heartbeat:*", "calc:*"];

export default function App() {
  const { latest, connected } = useStream({ patterns: PATTERNS });

  const heartbeats = useMemo(() => {
    const hb: Record<string, number> = {};
    for (const [stream, msg] of latest) {
      if (stream.startsWith("heartbeat:")) {
        hb[stream.replace("heartbeat:", "")] = msg.timestamp;
      }
    }
    return hb;
  }, [latest]);

  return (
    <div className="app">
      <header className="app-header">
        <h1>fin-kit Dashboard</h1>
        <StatusBar connected={connected} heartbeats={heartbeats} />
      </header>
      <main className="chart-grid">
        {TREASURY_INSTRUMENTS.map(({ code, label }) => {
          const stream = `market:futures:${code}`;
          const msg = latest.get(stream);
          return (
            <TreasuryChart
              key={code}
              stream={stream}
              label={label}
              latest={msg ? (msg.data as any) : undefined}
            />
          );
        })}
      </main>
    </div>
  );
}
```

- [x] **Step 10: Create `web/dashboard/src/components/StatusBar.tsx`**

```tsx
interface StatusBarProps {
  connected: boolean;
  heartbeats: Record<string, number>;
}

export function StatusBar({ connected, heartbeats }: StatusBarProps) {
  const now = Date.now();

  return (
    <div className="status-bar">
      <span className={`status-dot ${connected ? "green" : "red"}`} />
      <span>{connected ? "Connected" : "Disconnected"}</span>
      {Object.entries(heartbeats).map(([service, ts]) => {
        const age = (now - ts) / 1000;
        const color = age < 5 ? "green" : age < 15 ? "yellow" : "red";
        return (
          <span key={service} className="heartbeat-item">
            <span className={`status-dot ${color}`} />
            {service}
          </span>
        );
      })}
    </div>
  );
}
```

- [x] **Step 11: Create `web/dashboard/src/App.css`**

```css
* { margin: 0; padding: 0; box-sizing: border-box; }

body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace;
  background: #0f0f1a;
  color: #e0e0e0;
}

.app { padding: 16px; }

.app-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.app-header h1 {
  font-size: 1.2rem;
  font-weight: 500;
}

.status-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  font-size: 0.8rem;
}

.status-dot {
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  margin-right: 4px;
}

.status-dot.green { background: #4caf50; }
.status-dot.yellow { background: #ffc107; }
.status-dot.red { background: #f44336; }

.heartbeat-item {
  display: flex;
  align-items: center;
}

.chart-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.chart-panel {
  background: #1a1a2e;
  border: 1px solid #2a2a3e;
  border-radius: 6px;
  padding: 8px;
}

.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 4px;
  padding: 0 4px;
}

.chart-label {
  font-size: 0.85rem;
  color: #888;
}

.chart-price {
  font-size: 1.1rem;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
  color: #4fc3f7;
}
```

- [x] **Step 12: Install deps and run the dashboard**

```bash
cd web/dashboard
bun install
bun run dev
```

Expected: Vite dev server starts on http://localhost:5173. Open in browser.

- [x] **Step 13: Verify end-to-end with mock publisher + Bun server**

Ensure these are running:
1. Redis: `docker compose up -d`
2. Mock publisher: `cd adapters/mock && uv run publish_mock_data.py --interval 0.5`
3. Bun server: `cd web/server && bun run dev`
4. Dashboard: `cd web/dashboard && bun run dev`

Open http://localhost:5173 in browser.

Expected: Four charts (TU, FV, TY, US) rendering live price updates. Status bar shows green "Connected" dot and green "mock-publisher" heartbeat.

- [x] **Step 14: Commit**

```bash
git add web/dashboard/
git commit -m "feat: add React dashboard with live treasury chart panels"
```

---

## Chunk 5: C++ Streaming Service

The fin-kit streaming service reads raw market data from Redis Streams, runs calculations using existing fin-kit modules, and writes results back to Redis Streams. For this PoC, it computes treasury futures implied rates (100 - price) as a proof of the pipeline.

**Dependencies on prior chunks:** Chunk 1, Task 1 (Redis running) and Chunk 1, Task 2 (redis-plus-plus built).

### Task 7: Redis consumer, calculation dispatch, and heartbeat

**Files:**
- Modify: `services/finkit-stream/CMakeLists.txt`
- Rewrite: `services/finkit-stream/main.cpp`

- [x] **Step 1: Update `services/finkit-stream/CMakeLists.txt`**

Replace the full contents with:

```cmake
add_executable(finkit-stream main.cpp)

target_link_libraries(finkit-stream PRIVATE
    finkit::core
    finkit::data
    finkit::analysis
    redis++::redis++_static
)

target_compile_features(finkit-stream PRIVATE cxx_std_20)
target_compile_definitions(finkit-stream PRIVATE SPDLOG_FMT_EXTERNAL)
```

Note: Use `redis++::redis++_static` (or `redis++::redis++` — check Conan output). `finkit::analysis` linked for future bond basis calculations. `SPDLOG_FMT_EXTERNAL` matches existing module convention.

- [x] **Step 2: Write `services/finkit-stream/main.cpp`**

```cpp
#include <sw/redis++/redis++.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

import finkit.core;

namespace redis = sw::redis;
using Clock = std::chrono::steady_clock;

static std::atomic<bool> running{true};
void handle_signal(int) { running = false; }

// ── Configuration ──────────────────────────────────────────────────────────

struct Config {
    std::string redis_host = "localhost";
    int redis_port = 6379;
    int calc_window_ms = 100;
};

Config load_config() {
    Config c;
    if (auto* v = std::getenv("REDIS_HOST")) c.redis_host = v;
    if (auto* v = std::getenv("REDIS_PORT")) c.redis_port = std::atoi(v);
    if (auto* v = std::getenv("FINKIT_STREAM_CALC_WINDOW_MS")) c.calc_window_ms = std::atoi(v);
    return c;
}

// ── Stream helpers ─────────────────────────────────────────────────────────

static const std::string GROUP = "finkit";
static const std::string CONSUMER = "finkit-1";
static constexpr long long MAXLEN = 10'000;

void ensure_group(redis::Redis& r, const std::string& stream) {
    try {
        r.xgroup_create(stream, GROUP, "$", true);  // mkstream=true
    } catch (const redis::ReplyError&) {
        // BUSYGROUP — already exists
    }
}

// ── Buffered market data ───────────────────────────────────────────────────

struct FuturesQuote {
    double price;
    long long timestamp;
};

// ── Calculations ───────────────────────────────────────────────────────────

void run_calculations(
    redis::Redis& r,
    const std::unordered_map<std::string, FuturesQuote>& buffer
) {
    for (const auto& [code, quote] : buffer) {
        // PoC calculation: implied rate = 100 - price
        double implied_rate = 100.0 - quote.price;

        std::string stream = "calc:implied_rate:" + code;
        r.xadd(stream, "*",
            {{"rate", std::to_string(implied_rate)},
             {"price", std::to_string(quote.price)},
             {"timestamp", std::to_string(quote.timestamp)}},
            MAXLEN, true);  // approximate trimming
    }
}

// ── Main loop ──────────────────────────────────────────────────────────────

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    auto config = load_config();
    auto uri = "tcp://" + config.redis_host + ":" + std::to_string(config.redis_port);

    std::cout << "[finkit-stream] connecting to Redis at " << uri << "\n";
    redis::Redis r(uri);

    // Streams to consume — treasury futures from mock publisher or LSEG adapter
    std::vector<std::string> input_streams = {
        "market:futures:TU",
        "market:futures:FV",
        "market:futures:TY",
        "market:futures:US",
    };

    for (const auto& s : input_streams) {
        ensure_group(r, s);
    }

    std::cout << "[finkit-stream] listening on " << input_streams.size()
              << " streams (window=" << config.calc_window_ms << "ms)\n";

    auto last_calc = Clock::now();
    auto last_heartbeat = Clock::now();
    std::unordered_map<std::string, FuturesQuote> buffer;

    while (running) {
        // Build XREADGROUP input: vector of {stream, ">"}
        std::vector<std::pair<std::string, std::string>> stream_ids;
        for (const auto& s : input_streams) {
            stream_ids.emplace_back(s, ">");
        }

        // Block for up to calc_window_ms
        // Note: these type aliases approximate the redis-plus-plus API.
        // Verify exact types against <sw/redis++/redis++.h> after install.
        using Item = std::pair<std::string, std::vector<std::pair<std::string, std::string>>>;
        using ItemStream = std::vector<Item>;
        std::unordered_map<std::string, ItemStream> results;

        try {
            r.xreadgroup(GROUP, CONSUMER,
                stream_ids.begin(), stream_ids.end(),
                std::chrono::milliseconds(config.calc_window_ms),
                100,  // count
                std::inserter(results, results.end()));
        } catch (const redis::Error& e) {
            std::cerr << "[finkit-stream] XREADGROUP error: " << e.what() << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        // Parse incoming messages into buffer
        for (const auto& [stream, items] : results) {
            // Extract instrument code from stream name: "market:futures:TY" → "TY"
            auto code = stream.substr(stream.rfind(':') + 1);

            for (const auto& [id, fields] : items) {
                FuturesQuote quote{};
                for (const auto& [key, val] : fields) {
                    if (key == "price") quote.price = std::stod(val);
                    if (key == "timestamp") quote.timestamp = std::stoll(val);
                }
                buffer[code] = quote;  // latest wins

                // ACK the message
                r.xack(stream, GROUP, id);
            }
        }

        // Fire calculations when window expires
        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_calc);
        if (elapsed.count() >= config.calc_window_ms && !buffer.empty()) {
            run_calculations(r, buffer);
            buffer.clear();
            last_calc = now;
        }

        // Heartbeat every 2 seconds
        auto hb_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat);
        if (hb_elapsed.count() >= 2) {
            auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            r.xadd("heartbeat:finkit-stream", "*",
                {{"status", "ok"}, {"timestamp", std::to_string(ts)}},
                1000, true);
            last_heartbeat = now;
        }
    }

    std::cout << "\n[finkit-stream] shutting down\n";
    return 0;
}
```

- [x] **Step 3: Build the streaming service**

```bash
cmake --build build/build/Release --target finkit-stream
```

Expected: Compiles successfully. If module/header mixing issues arise, verify that `main.cpp` is NOT declared as a module interface unit (no `export module` line).

- [x] **Step 4: Run end-to-end test with mock publisher**

Terminal 1 — mock publisher:
```bash
cd adapters/mock && uv run publish_mock_data.py --interval 0.5
```

Terminal 2 — fin-kit stream service:
```bash
./build/build/Release/services/finkit-stream/finkit-stream
```

Terminal 3 — verify calc streams appear:
```bash
docker compose exec redis redis-cli XLEN calc:implied_rate:TY
docker compose exec redis redis-cli XRANGE calc:implied_rate:TY - + COUNT 3
```

Expected: `calc:implied_rate:TY` has entries with `rate`, `price`, and `timestamp` fields. The rate should be approximately `100 - 110.75 ≈ -10.75` (negative because treasury futures trade above 100).

Also verify heartbeat:
```bash
docker compose exec redis redis-cli XRANGE heartbeat:finkit-stream - + COUNT 1
```

Expected: Entry with `status=ok` and a recent timestamp.

- [x] **Step 5: Commit**

```bash
git add services/finkit-stream/
git commit -m "feat: add fin-kit streaming service with Redis consumer and calculation loop"
```

---

## Chunk 6: LSEG Live Adapter

Adds jl-lseg-toolkit as a git submodule and provides a thin Python adapter that polls LSEG for live US Treasury futures data and writes to Redis Streams.

**Dependencies:** Chunk 1 (Redis running). Requires LSEG Workspace running locally on port 9000 and a valid app key.

### Task 8: Add jl-lseg-toolkit submodule

**Files:**
- Modify: `.gitmodules`

- [x] **Step 1: Add the submodule**

```bash
git submodule add -b develop https://github.com/jlipworth/jl-lseg-toolkit.git adapters/lseg/jl-lseg-toolkit
```

This clones the toolkit into `adapters/lseg/jl-lseg-toolkit/` and creates/updates `.gitmodules`.

- [x] **Step 2: Verify the submodule**

```bash
ls adapters/lseg/jl-lseg-toolkit/pyproject.toml
```

Expected: File exists. The toolkit is checked out at the `develop` branch HEAD.

- [x] **Step 3: Commit**

```bash
git add .gitmodules adapters/lseg/jl-lseg-toolkit
git commit -m "chore: add jl-lseg-toolkit as git submodule"
```

---

### Task 9: LSEG adapter script

**Files:**
- Create: `adapters/lseg/ingest.py`
- Create: `adapters/lseg/pyproject.toml`

- [x] **Step 1: Create `adapters/lseg/pyproject.toml`**

```toml
[project]
name = "finkit-lseg-adapter"
version = "0.1.0"
requires-python = ">=3.12"
dependencies = [
    "redis>=5.0",
]

[tool.uv.sources]
jl-lseg-toolkit = { path = "jl-lseg-toolkit" }
```

The adapter imports `lseg_toolkit` from the submodule via a `uv` path source — not from PyPI.

- [x] **Step 2: Create `adapters/lseg/ingest.py`**

```python
#!/usr/bin/env python3
"""LSEG → Redis Streams adapter for US Treasury futures.

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
import time

import redis
from lseg_toolkit.timeseries import LSEGDataClient

MAXLEN = 10_000

# CME product code → LSEG RIC mapping (continuous front-month contracts)
# From jl-lseg-toolkit/src/lseg_toolkit/timeseries/constants.py
INSTRUMENTS = {
    "TU": "TUc1",   # 2Y T-Note
    "FV": "FVc1",   # 5Y T-Note
    "TY": "TYc1",   # 10Y T-Note
    "US": "USc1",   # 30Y T-Bond
}


def main():
    parser = argparse.ArgumentParser(description="LSEG treasury adapter")
    parser.add_argument("--interval", type=float, default=5.0,
                        help="Seconds between polls (default: 5)")
    parser.add_argument("--redis-host", default=os.getenv("REDIS_HOST", "localhost"))
    parser.add_argument("--redis-port", type=int,
                        default=int(os.getenv("REDIS_PORT", "6379")))
    args = parser.parse_args()

    r = redis.Redis(host=args.redis_host, port=args.redis_port, decode_responses=True)
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
                # Fetch latest snapshot for all RICs
                df = client.get_history(
                    rics=rics,
                    fields=["TRDPRC_1", "SETTLE", "HIGH_1", "LOW_1"],
                    interval="daily",
                    count=1,
                )

                if df is not None and not df.empty:
                    for code, ric in INSTRUMENTS.items():
                        row = df[df.index.get_level_values("Instrument") == ric]
                        if row.empty:
                            continue

                        price = row.iloc[0].get("TRDPRC_1") or row.iloc[0].get("SETTLE")
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
```

**Note on `LSEGDataClient.get_history()` API:** The exact parameters may differ from what's shown. Check `jl-lseg-toolkit/src/lseg_toolkit/timeseries/client.py` for the current signature. The `count=1` + `interval="daily"` pattern fetches the latest snapshot. If the toolkit's API doesn't support `count`, use `start_date=today` instead.

- [x] **Step 3: Sync deps and verify the import works**

```bash
cd adapters/lseg
uv sync
uv run python -c "from lseg_toolkit.timeseries import LSEGDataClient; print('import OK')"
```

Expected: Prints `import OK`. `uv sync` installs redis and resolves jl-lseg-toolkit from the submodule path source.

- [ ] **Step 4: Test with LSEG Workspace (manual)**

This requires LSEG Workspace running locally. If available:

```bash
cd adapters/lseg
uv run ingest.py --interval 5
```

Expected: Adapter connects, polls, and publishes to Redis Streams. Verify with:
```bash
docker compose exec redis redis-cli XRANGE market:futures:TY - + COUNT 3
```

If LSEG Workspace is not available, use the mock publisher from Chunk 2 instead.

- [x] **Step 5: Commit**

```bash
git add adapters/lseg/ingest.py adapters/lseg/pyproject.toml
git commit -m "feat: add LSEG treasury adapter using jl-lseg-toolkit submodule"
```

---

## End-to-End Verification

After all chunks are complete, run the full stack:

```bash
# Terminal 1 — Redis
docker compose up -d

# Terminal 2 — Mock publisher (or LSEG adapter)
cd adapters/mock && uv run publish_mock_data.py --interval 0.5

# Terminal 3 — fin-kit streaming service
./build/build/Release/services/finkit-stream/finkit-stream

# Terminal 4 — Bun WebSocket server
cd web/server && bun run dev

# Terminal 5 — React dashboard
cd web/dashboard && bun run dev
```

Open http://localhost:5173. Verify:
1. Four treasury futures charts update in real-time
2. Status bar shows green dots for WebSocket connection, mock-publisher heartbeat, and finkit-stream heartbeat
3. `calc:implied_rate:*` streams appear in Redis (written by fin-kit, relayed through Bun to dashboard)

---

## What's Next (Not In This Plan)

- **Dashboard panel design:** Additional panels for Fed probabilities, bond basis, yield curves
- **Real calculations:** Replace PoC implied-rate math with bond basis via `finkit::analysis`, curve bootstrapping via `finkit::bootstrap`
- **Stream persistence:** Replace `stream_log` catch-all table with typed inserts into the actual TimescaleDB tables per the spec's stream-to-table mapping
- **Testing:** Unit and integration tests for the C++ streaming service, Bun server, and adapter
- **Startup automation:** `start.sh` / `stop.sh` / Makefile targets
- **Latency monitoring:** Metrics beyond heartbeats
- **Kafka graduation:** If/when deployment becomes shared
