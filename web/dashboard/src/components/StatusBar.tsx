interface StatusBarProps {
  connected: boolean;
  heartbeats: Record<string, number>;
}

export function StatusBar({ connected, heartbeats }: StatusBarProps) {
  const now = Date.now();

  return (
    <div className="status-bar">
      <span className={`status-dot ${connected ? "green" : "red"}`} />
      <span>{connected ? "Connected" : "Disconnected"}</span>
      {Object.entries(heartbeats).map(([service, ts]) => {
        const age = (now - ts) / 1000;
        const color = age < 5 ? "green" : age < 15 ? "yellow" : "red";
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
