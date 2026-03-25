import { useEffect, useRef, useState, useCallback } from "react";

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
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectRef = useRef<ReturnType<typeof setTimeout>>();
  const [connected, setConnected] = useState(false);
  const [latest, setLatest] = useState<Map<string, StreamMessage>>(new Map());

  const patternsKey = patterns.join(",");

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
      reconnectRef.current = setTimeout(connect, 2000);
    };

    ws.onerror = () => ws.close();

    wsRef.current = ws;
  }, [wsUrl, patternsKey]);

  useEffect(() => {
    connect();
    return () => {
      clearTimeout(reconnectRef.current);
      wsRef.current?.close();
    };
  }, [connect]);

  return { latest, connected };
}
