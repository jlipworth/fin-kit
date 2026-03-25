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
