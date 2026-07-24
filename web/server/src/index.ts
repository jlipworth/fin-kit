import { initRedis, startConsumer } from "./redis";
import { websocketHandler, fanOut, type WsData } from "./ws";
import { fetchRequestHandler } from "@trpc/server/adapters/fetch";
import { appRouter } from "./trpc";
import { initPersistence, persistMessage } from "./persist";

function envInt(value: string | undefined, fallback: number): number {
  const n = parseInt(value ?? "", 10);
  return Number.isFinite(n) ? n : fallback;
}

const WEB_PORT = envInt(process.env.WEB_PORT, 3000);
const REDIS_HOST = process.env.REDIS_HOST || "localhost";
const REDIS_PORT = envInt(process.env.REDIS_PORT, 6379);
const TSDB_HOST = process.env.TSDB_HOST || process.env.POSTGRES_HOST;

initRedis(REDIS_HOST, REDIS_PORT);
await initPersistence(
  TSDB_HOST
    ? {
        host: TSDB_HOST,
        port: envInt(process.env.TSDB_PORT || process.env.POSTGRES_PORT, 5432),
        database: process.env.TSDB_DATABASE || process.env.POSTGRES_DB || "timeseries",
        username: process.env.TSDB_USER || process.env.POSTGRES_USER || "postgres",
        password: process.env.TSDB_PASSWORD || process.env.POSTGRES_PASSWORD || "",
      }
    : undefined,
);

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

    // tRPC API
    if (url.pathname.startsWith("/api/trpc")) {
      return fetchRequestHandler({
        endpoint: "/api/trpc",
        req,
        router: appRouter,
        createContext: () => ({}),
      });
    }

    return new Response("Not found", { status: 404 });
  },

  websocket: websocketHandler,
});

// Start Redis consumer → fan out to WebSocket clients and persist.
// Persistence is awaited so the message is not ACKed before the write lands.
startConsumer(async (stream, data) => {
  fanOut(stream, data);
  await persistMessage(stream, data);
});

console.log(`[server] listening on http://localhost:${server.port}`);
console.log(`[server] WebSocket at ws://localhost:${server.port}/ws`);
