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
