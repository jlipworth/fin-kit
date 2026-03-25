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
              latest={msg ? { price: msg.data.price, timestamp: Number(msg.data.timestamp) } : undefined}
            />
          );
        })}
      </main>
    </div>
  );
}
