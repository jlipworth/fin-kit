# Local Services

This page covers the local infrastructure used by dashboard, streaming, and
data-backed workflows.

## What uses local services?

- **Core C++ build/tests:** does **not** require Docker
- **Redis streams / dashboard / streaming service:** requires Redis
- **Shared market data workflows:** require TimescaleDB credentials

## Docker Desktop on WSL2

If you are using WSL2 on Windows:

1. Install Docker Desktop on Windows
2. Enable **Settings → Resources → WSL Integration**
3. Turn on integration for this distro
4. Verify that `docker ps` works from the WSL shell

If WSL integration is disabled, Docker Desktop may still run on Windows, but the
`docker` command inside WSL will not work correctly. That is a setup problem,
not a repo problem, and it should be fixed before working on local services.

If `docker.exe version` also fails with a named-pipe / daemon error, Docker
Desktop itself is not running yet. Start Docker Desktop on Windows first, then
re-run the checks below.

If `docker ps` fails with `permission denied while trying to connect to the
docker API at unix:///var/run/docker.sock`, your WSL user is not using the
updated `docker` group yet. Start a new shell (or log out/in) so the group
membership takes effect.

## Redis for dashboard/streaming workflows

The repo includes a root `docker-compose.yml` that starts Redis with AOF
persistence enabled:

```bash
docker compose up -d
docker compose ps
docker compose exec redis redis-cli ping
```

Expected response:

```text
PONG
```

If you are on WSL2, run the commands from the WSL shell after enabling Docker
Desktop integration. If you prefer, you can also invoke Docker Desktop's Windows
CLI directly from WSL via `docker.exe`, but the preferred path is native WSL
integration.

Fallback from WSL if integration is still being repaired:

```bash
WIN_PWD=$(wslpath -w "$PWD")
'/mnt/c/Program Files/Docker/Docker/resources/bin/docker.exe' compose --project-directory "$WIN_PWD" up -d
```

## TimescaleDB

fin-kit reads and writes shared data in TimescaleDB for data-backed workflows.
This repository does not provision a local TimescaleDB container by default.

Use the existing environment variables from [SETUP.md](SETUP.md):

- `TSDB_HOST`
- `TSDB_PORT`
- `TSDB_DATABASE`
- `TSDB_USER`
- `TSDB_PASSWORD`

If you are using Infisical, keep the same secret source as the rest of the
project and export the credentials before running the build or services that
need them.

For agent workflows, use the repo-local TSDB guidance in
`skills/timescaledb-access/SKILL.md` (and `.claude/agents/timescaledb-access.md`
for Claude).

## Quick checks

- `docker compose version`
- `docker ps`
- `docker compose exec redis redis-cli ping`

If these fail in WSL, check Docker Desktop first, then WSL integration, then
confirm the daemon is running. If the daemon is up but WSL still says
`permission denied`, restart your shell so the `docker` group membership is
picked up.
