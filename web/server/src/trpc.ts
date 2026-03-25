import { initTRPC, TRPCError } from "@trpc/server";
import * as v from "valibot";
import { getSql } from "./persist";

const t = initTRPC.create();

export const appRouter = t.router({
  history: t.router({
    /**
     * Fetch historical stream data from TimescaleDB.
     * Replaces: GET /api/history/:stream?from=<ts>&to=<ts>&limit=1000
     * Returns same { stream, data, timestamp } shape as WebSocket messages.
     */
    byStream: t.procedure
      .input(
        v.parser(
          v.object({
            stream: v.string(),
            from: v.optional(v.number()),
            to: v.optional(v.number()),
            limit: v.optional(v.pipe(v.number(), v.maxValue(10_000))),
          }),
        ),
      )
      .query(async ({ input }) => {
        const sql = getSql();
        if (!sql) {
          throw new TRPCError({
            code: "PRECONDITION_FAILED",
            message: "TimescaleDB not configured",
          });
        }

        const limit = input.limit ?? 1000;

        const rows = await sql`
          SELECT stream, data, ts
          FROM stream_log
          WHERE stream LIKE ${input.stream.replace("*", "%")}
            ${input.from ? sql`AND ts >= to_timestamp(${input.from} / 1000.0)` : sql``}
            ${input.to ? sql`AND ts <= to_timestamp(${input.to} / 1000.0)` : sql``}
          ORDER BY ts DESC
          LIMIT ${limit}
        `;

        return rows.map((r: any) => ({
          stream: r.stream as string,
          data: r.data as Record<string, string>,
          timestamp: new Date(r.ts).getTime(),
        }));
      }),
  }),
});

/** Export the router type for the dashboard client */
export type AppRouter = typeof appRouter;
