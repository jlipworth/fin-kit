import Redis from "ioredis";

const STREAM_PREFIXES = ["market:", "calc:", "heartbeat:"];
const CONSUMER_GROUP = "dashboard";
const CONSUMER_NAME = "dashboard-1";
const SCAN_INTERVAL_MS = 5_000;

type StreamEntries = [id: string, fields: string[]][];
type XReadGroupResult = [stream: string, entries: StreamEntries][] | null;

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

let discovering = false;

/** Discover streams by scanning for keys matching our prefixes */
async function discoverStreams() {
  if (discovering) return; // don't stack scans if one is slow or Redis is down
  discovering = true;
  try {
    for (const prefix of STREAM_PREFIXES) {
      let cursor = "0";
      do {
        const [next, keys] = await redis.scan(cursor, "MATCH", `${prefix}*`, "COUNT", 100);
        cursor = next;
        for (const key of keys) {
          if (knownStreams.has(key)) continue;
          // Create the consumer group BEFORE advertising the stream to the
          // consumer loop — otherwise XREADGROUP races the creation and the
          // whole multi-stream read fails with NOGROUP.
          try {
            await redis.xgroup("CREATE", key, CONSUMER_GROUP, "0", "MKSTREAM");
          } catch (err) {
            if (!String(err).includes("BUSYGROUP")) {
              console.error(`[redis] group create failed for ${key}:`, err);
              continue; // retry on the next scan
            }
          }
          knownStreams.add(key);
          console.log(`[redis] discovered stream: ${key}`);
        }
      } while (cursor !== "0");
    }
  } finally {
    discovering = false;
  }
}

/**
 * Start the consumer loop.
 * Calls onMessage(stream, data) for every new message across all discovered
 * streams, awaiting it before the message is ACKed (so fan-out + persistence
 * complete before the message leaves the pending list).
 * Periodically scans for new streams.
 */
export async function startConsumer(
  onMessage: (stream: string, data: Record<string, string>) => void | Promise<void>,
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
      const results = (await subscriberRedis.xreadgroup(
        "GROUP", CONSUMER_GROUP, CONSUMER_NAME,
        "COUNT", 100,
        "BLOCK", 1000,
        "STREAMS", ...streams, ...streams.map(() => ">"),
      )) as XReadGroupResult;

      if (results) {
        for (const [stream, messages] of results) {
          for (const [id, fields] of messages) {
            // fields is [key, value, key, value, ...]
            const data: Record<string, string> = {};
            for (let i = 0; i < fields.length; i += 2) {
              data[fields[i]] = fields[i + 1];
            }
            await onMessage(stream, data);
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
