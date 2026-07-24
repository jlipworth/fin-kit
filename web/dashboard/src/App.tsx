import { useMemo } from "react";
import { useStream } from "./hooks/useStream";
import { TreasuryChart, type HistoryPoint } from "./components/TreasuryChart";
import { StatusBar } from "./components/StatusBar";
import { trpc } from "./lib/trpc";

const TREASURY_INSTRUMENTS = [
  { code: "TU", label: "2Y T-Note (TU)" },
  { code: "FV", label: "5Y T-Note (FV)" },
  { code: "TY", label: "10Y T-Note (TY)" },
  { code: "US", label: "30Y T-Bond (US)" },
];

const PATTERNS = ["market:futures:*", "heartbeat:*", "calc:*"];

interface InstrumentPanelProps {
  code: string;
  label: string;
  latest: { price: string; timestamp: number } | undefined;
  connected: boolean;
}

function InstrumentPanel({ code, label, latest, connected }: InstrumentPanelProps) {
  const stream = `market:futures:${code}`;
  // Seed the chart with recent history; the server answers PRECONDITION_FAILED
  // when TimescaleDB is not configured, in which case the chart is live-only.
  const historyQuery = trpc.history.byStream.useQuery(
    { stream, limit: 500 },
    { retry: false, refetchOnWindowFocus: false, staleTime: Infinity },
  );

  const history = useMemo<HistoryPoint[] | undefined>(
    () =>
      historyQuery.data
        ?.filter((m) => m.data.price !== undefined)
        .map((m) => ({ price: m.data.price, timestamp: m.timestamp })),
    [historyQuery.data],
  );

  return (
    <TreasuryChart label={label} latest={latest} history={history} dimmed={!connected} />
  );
}

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
            <InstrumentPanel
              key={code}
              code={code}
              label={label}
              connected={connected}
              latest={msg ? { price: msg.data.price, timestamp: msg.timestamp } : undefined}
            />
          );
        })}
      </main>
    </div>
  );
}
