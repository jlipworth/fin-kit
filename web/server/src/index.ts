import { initRedis, startConsumer } from "./redis";
import { websocketHandler, fanOut, type WsData } from "./ws";
import { fetchRequestHandler } from "@trpc/server/adapters/fetch";
import { appRouter } from "./trpc";
import { initPersistence, persistMessage } from "./persist";

const WEB_PORT = parseInt(process.env.WEB_PORT || "3000");
const REDIS_HOST = process.env.REDIS_HOST || "localhost";
const REDIS_PORT = parseInt(process.env.REDIS_PORT || "6379");

initRedis(REDIS_HOST, REDIS_PORT);
await initPersistence();

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

// Start Redis consumer → fan out to WebSocket clients and persist
startConsumer((stream, data) => {
  fanOut(stream, data);
  persistMessage(stream, data);
});

console.log(`[server] listening on http://localhost:${server.port}`);
console.log(`[server] WebSocket at ws://localhost:${server.port}/ws`);
