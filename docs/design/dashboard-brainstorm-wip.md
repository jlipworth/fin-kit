# Dashboard & Data Pipeline — Brainstorm WIP

**Status:** In-progress discussion, not yet a design spec.

## What we've established

- **Goal:** Live dashboard showing both raw market data (LSEG/BBG) and fin-kit calculated analytics as first-class citizens
- **fin-kit role:** Runs as a persistent service — start it alongside your data feed, consumes live data, runs calculations, publishes results
- **Latency target:** Sub-second capable infrastructure (individual calculations can be slower as needed)
- **Dashboard style:** Mostly purpose-built panels (Fed probabilities, curves, basis) with ability to add custom panels over time
- **Historical data:** Dashboard should also show prior data from TimescaleDB (not just live)
- **Deployment:** Local single-user for now, possibly shared later
- **Backtesting:** Separate workflow, park for now — but system should be extensible to it later
- **LSEG connectivity:** TBD — either Real-Time SDK (WebSocket) or Python library; not yet tested for streaming

## Architecture recommendation (not yet approved)

**Redis Streams** as the message backbone (Option B of three presented):

- **Option A — Redis pub/sub:** Simplest, but fire-and-forget. Missed messages on restart.
- **Option B — Redis Streams (recommended):** Still just Redis. Messages persist, consumers can replay on restart. Sub-millisecond. Consumer groups for future scaling.
- **Option C — Kafka + Redis:** Industry standard but heavy for local single-user (JVM, ~1GB+ RAM). Graduate to this if/when going shared.

## Still to discuss

- Confirm architecture choice (A/B/C)
- WebSocket server / web framework for the dashboard (e.g. FastAPI + WebSocket, or something else)
- Frontend tech (Grafana? Custom with D3/Plotly? React?)
- How C++ fin-kit subscribes to and publishes on Redis Streams (hiredis? redis-plus-plus?)
- Specific panels / views for the initial dashboard
- How historical TimescaleDB queries integrate with live stream
- Visual companion didn't work (likely WSL port forwarding) — try again or go text-only

## Data flow sketch

```
LSEG feed → [Python ingestion] → Redis Streams (raw market data)
                                        ↓
                                  [fin-kit service]
                                        ↓
                                  Redis Streams (calculated analytics)
                                        ↓
                                  [WebSocket server] → Browser dashboard
                                        ↑
                                  TimescaleDB (historical queries)
```
