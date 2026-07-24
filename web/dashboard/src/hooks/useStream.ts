import { useEffect, useState } from "react";

interface StreamMessage {
  stream: string;
  data: Record<string, string>;
  timestamp: number;
}

interface UseStreamOptions {
  url?: string;
  patterns: string[];
}

interface UseStreamResult {
  latest: Map<string, StreamMessage>;
  connected: boolean;
}

export function useStream({ url, patterns }: UseStreamOptions): UseStreamResult {
  const wsUrl = url || `ws://${window.location.host}/ws`;
  const [connected, setConnected] = useState(false);
  const [latest, setLatest] = useState<Map<string, StreamMessage>>(new Map());

  const patternsKey = patterns.join(",");

  useEffect(() => {
    // Each effect run owns one connection chain. `stopped` keeps a socket's
    // async onclose (which fires after cleanup) from re-arming the reconnect
    // timer with a stale closure and leaking zombie connections.
    let stopped = false;
    let ws: WebSocket | null = null;
    let reconnectTimer: ReturnType<typeof setTimeout> | undefined;
    const subscribe = patternsKey.length > 0 ? patternsKey.split(",") : [];

    const connect = () => {
      ws = new WebSocket(wsUrl);

      ws.onopen = () => {
        if (stopped) return;
        setConnected(true);
        ws?.send(JSON.stringify({ subscribe }));
      };

      ws.onmessage = (event) => {
        if (stopped) return;
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
        if (stopped) return;
        setConnected(false);
        reconnectTimer = setTimeout(connect, 2000);
      };

      ws.onerror = () => ws?.close();
    };

    connect();

    return () => {
      stopped = true;
      clearTimeout(reconnectTimer);
      ws?.close();
      setConnected(false);
    };
  }, [wsUrl, patternsKey]);

  return { latest, connected };
}
