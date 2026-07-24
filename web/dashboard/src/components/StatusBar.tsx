import { useState, useEffect } from "react";

interface StatusBarProps {
  connected: boolean;
  heartbeats: Record<string, number>;
}

export function StatusBar({ connected, heartbeats }: StatusBarProps) {
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), 2000);
    return () => clearInterval(id);
  }, []);

  return (
    <div className="status-bar">
      <span className={`status-dot ${connected ? "green" : "red"}`} />
      <span>{connected ? "Connected" : "Disconnected"}</span>
      {Object.entries(heartbeats).map(([service, ts]) => {
        const age = (now - ts) / 1000;
        // Slowest publisher (LSEG adapter) heartbeats every 5s, so green must
        // tolerate a full interval plus jitter without flickering yellow.
        const color = age < 10 ? "green" : age < 30 ? "yellow" : "red";
        return (
          <span key={service} className="heartbeat-item">
            <span className={`status-dot ${color}`} />
            {service}
          </span>
        );
      })}
    </div>
  );
}
