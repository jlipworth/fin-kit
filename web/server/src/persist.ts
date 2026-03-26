import postgres from "postgres";

/**
 * Stream name → TimescaleDB table mapping, per design spec.
 */
const STREAM_TABLE_MAP: Record<string, string> = {
  "market:fx:": "fx_spot",
  "market:rates:sofr": "rates_sofr_fixings",
  "market:rates:ois:": "rates_ois_quotes",
  "market:bonds:": "bonds_prices",
  "market:futures:": "futures_treasury",
  "calc:implied_rate:": "calculated_implied_rates",
  "calc:fed_probs:": "calculated_fed_probs",
  "calc:basis:bond:": "calculated_basis",
  "calc:basis:cip:": "calculated_basis",
  "calc:curves:sofr": "calculated_curves",
};

let sql: ReturnType<typeof postgres> | null = null;

/** Initialize TimescaleDB connection and ensure schema. Skips if TSDB_HOST is not set. */
export async function initPersistence() {
  const host = process.env.TSDB_HOST || process.env.POSTGRES_HOST;
  if (!host) {
    console.log("[persist] TSDB_HOST not set — persistence disabled");
    return;
  }

  sql = postgres({
    host,
    port: parseInt(process.env.TSDB_PORT || process.env.POSTGRES_PORT || "5432"),
    database: process.env.TSDB_DATABASE || process.env.POSTGRES_DB || "timeseries",
    username: process.env.TSDB_USER || process.env.POSTGRES_USER || "postgres",
    password: process.env.TSDB_PASSWORD || process.env.POSTGRES_PASSWORD || "",
  });

  // Create the catch-all stream log table if it doesn't exist.
  // This is a PoC approach — typed per-table inserts are a follow-up.
  await sql`
    CREATE TABLE IF NOT EXISTS stream_log (
      id BIGINT GENERATED ALWAYS AS IDENTITY,
      stream TEXT NOT NULL,
      table_name TEXT NOT NULL,
      data JSONB NOT NULL,
      ts TIMESTAMPTZ NOT NULL DEFAULT NOW()
    )
  `;
  await sql`
    CREATE INDEX IF NOT EXISTS idx_stream_log_stream_ts ON stream_log (stream, ts DESC)
  `;

  console.log(`[persist] connected to TimescaleDB at ${host}`);
}

/** Resolve a stream name to its TimescaleDB table */
export function resolveTable(stream: string): string | null {
  for (const [prefix, table] of Object.entries(STREAM_TABLE_MAP)) {
    if (stream === prefix || stream.startsWith(prefix)) return table;
  }
  return null;
}

/** Persist a stream message to TimescaleDB. Best-effort — logs errors, does not throw. */
export async function persistMessage(stream: string, data: Record<string, string>) {
  if (!sql) return;

  const table = resolveTable(stream);
  if (!table) return;

  try {
    await sql`
      INSERT INTO stream_log (stream, table_name, data, ts)
      VALUES (${stream}, ${table}, ${JSON.stringify(data)}, NOW())
    `;
  } catch (err) {
    console.error(`[persist] error writing to ${table}:`, err);
  }
}

/** Get postgres connection for historical queries */
export function getSql() {
  return sql;
}
