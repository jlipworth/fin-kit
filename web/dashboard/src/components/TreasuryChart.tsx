import { useEffect, useRef } from "react";
import { createChart, type IChartApi, type ISeriesApi, type LineData, type Time } from "lightweight-charts";

interface TreasuryChartProps {
  latest: { price: string; timestamp: number } | undefined;
  label: string;
}

export function TreasuryChart({ latest, label }: TreasuryChartProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const chartRef = useRef<IChartApi | null>(null);
  const seriesRef = useRef<ISeriesApi<"Line"> | null>(null);

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

  useEffect(() => {
    if (!latest || !seriesRef.current) return;
    const point: LineData<Time> = {
      time: (latest.timestamp / 1000) as Time,
      value: parseFloat(latest.price),
    };
    seriesRef.current.update(point);
  }, [latest]);

  return (
    <div className="chart-panel">
      <div className="chart-header">
        <span className="chart-label">{label}</span>
        {latest && (
          <span className="chart-price">{parseFloat(latest.price).toFixed(4)}</span>
        )}
      </div>
      <div ref={containerRef} />
    </div>
  );
}
