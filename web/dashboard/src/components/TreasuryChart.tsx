import { useEffect, useRef } from "react";
import { createChart, type IChartApi, type ISeriesApi, type LineData, type Time } from "lightweight-charts";

export interface HistoryPoint {
  price: string;
  timestamp: number;
}

interface TreasuryChartProps {
  latest: { price: string; timestamp: number } | undefined;
  history?: HistoryPoint[];
  label: string;
  dimmed?: boolean;
}

export function TreasuryChart({ latest, history, label, dimmed }: TreasuryChartProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const chartRef = useRef<IChartApi | null>(null);
  const seriesRef = useRef<ISeriesApi<"Line"> | null>(null);
  const lastTimeRef = useRef<number>(0);
  const historySeededRef = useRef(false);

  useEffect(() => {
    if (!containerRef.current) return;

    const chart = createChart(containerRef.current, {
      width: containerRef.current.clientWidth,
      height: 250,
      layout: {
        background: { color: "#1a1a2e" },
        textColor: "#e0e0e0",
      },
      grid: {
        vertLines: { color: "#2a2a3e" },
        horzLines: { color: "#2a2a3e" },
      },
      timeScale: { timeVisible: true, secondsVisible: true },
    });

    const series = chart.addLineSeries({
      color: "#4fc3f7",
      lineWidth: 2,
    });

    chartRef.current = chart;
    seriesRef.current = series;

    const onResize = () => {
      if (containerRef.current) {
        chart.applyOptions({ width: containerRef.current.clientWidth });
      }
    };
    window.addEventListener("resize", onResize);

    return () => {
      window.removeEventListener("resize", onResize);
      chart.remove();
    };
  }, []);

  // Seed the series once from tRPC history, if any arrives before live data.
  useEffect(() => {
    if (!history || historySeededRef.current || !seriesRef.current) return;
    if (lastTimeRef.current > 0) return; // live updates already started
    const points: LineData<Time>[] = [];
    for (const p of [...history].sort((a, b) => a.timestamp - b.timestamp)) {
      const time = Math.floor(p.timestamp / 1000);
      const value = parseFloat(p.price);
      if (!Number.isFinite(value) || time <= 0) continue;
      if (points.length > 0 && (points[points.length - 1].time as number) === time) {
        points[points.length - 1] = { time: time as Time, value };
      } else {
        points.push({ time: time as Time, value });
      }
    }
    if (points.length > 0) {
      seriesRef.current.setData(points);
      lastTimeRef.current = points[points.length - 1].time as number;
    }
    historySeededRef.current = true;
  }, [history]);

  useEffect(() => {
    if (!latest || !seriesRef.current) return;
    const time = Math.floor(latest.timestamp / 1000);
    const value = parseFloat(latest.price);
    // series.update() throws on out-of-order times; skip stale points.
    if (!Number.isFinite(value) || time < lastTimeRef.current) return;
    seriesRef.current.update({ time: time as Time, value });
    lastTimeRef.current = time;
  }, [latest]);

  return (
    <div className={`chart-panel${dimmed ? " dimmed" : ""}`}>
      <div className="chart-header">
        <span className="chart-label">{label}</span>
        {latest && (
          <span className="chart-price">{parseFloat(latest.price).toFixed(4)}</span>
        )}
      </div>
      <div className="chart-container" ref={containerRef} />
    </div>
  );
}
