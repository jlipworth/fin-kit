import Redis from "ioredis";

const STREAM_PREFIXES = ["market:", "calc:", "heartbeat:"];
const CONSUMER_GROUP = "dashboard";
const CONSUMER_NAME = "dashboard-1";
const SCAN_INTERVAL_MS = 5_000;

let redis: Redis;
let subscriberRedis: Redis; // separate connection for blocking XREADGROUP

/** Known streams (discovered via periodic SCAN) */
const knownStreams = new Set<string>();

export function getRedis(): Redis {
  return redis;
}

/** Initialize Redis connections */
export function initRedis(host: string, port: number) {
  redis = new Redis({ host, port, maxRetriesPerRequest: null });
  subscriberRedis = new Redis({ host, port, maxRetriesPerRequest: null });
}

/** Discover streams by scanning for keys matching our prefixes */
async function discoverStreams() {
  for (const prefix of STREAM_PREFIXES) {
    let cursor = "0";
    do {
      const [next, keys] = await redis.scan(cursor, "MATCH", `${prefix}*`, "COUNT", 100);
      cursor = next;
      for (const key of keys) {
        if (!knownStreams.has(key)) {
          knownStreams.add(key);
          // Create consumer group — ignore error if it already exists
          try {
            await redis.xgroup("CREATE", key, CONSUMER_GROUP, "0", "MKSTREAM");
          } catch {
            // BUSYGROUP — group already exists, fine
          }
          console.log(`[redis] discovered stream: ${key}`);
        }
      }
    } while (cursor !== "0");
  }
}

/**
 * Start the consumer loop.
 * Calls onMessage(stream, data) for every new message across all discovered streams.
 * Periodically scans for new streams.
 */
export async function startConsumer(
  onMessage: (stream: string, data: Record<string, string>) => void,
) {
  // Initial discovery
  await discoverStreams();

  // Periodic re-discovery
  setInterval(discoverStreams, SCAN_INTERVAL_MS);

  // Consumer loop
  while (true) {
    const streams = Array.from(knownStreams);
    if (streams.length === 0) {
      await new Promise((r) => setTimeout(r, 1000));
      continue;
    }

    try {
      const results = await subscriberRedis.xreadgroup(
        "GROUP", CONSUMER_GROUP, CONSUMER_NAME,
        "BLOCK", 1000,
        "COUNT", 100,
        "STREAMS", ...streams, ...streams.map(() => ">"),
      );

      if (results) {
        for (const [stream, messages] of results) {
          for (const [id, fields] of messages) {
            // fields is [key, value, key, value, ...]
            const data: Record<string, string> = {};
            for (let i = 0; i < fields.length; i += 2) {
              data[fields[i]] = fields[i + 1];
            }
            onMessage(stream, data);
            await redis.xack(stream, CONSUMER_GROUP, id);
          }
        }
      }
    } catch (err) {
      // Connection error — wait and retry
      console.error("[redis] consumer error:", err);
      await new Promise((r) => setTimeout(r, 2000));
    }
  }
}
