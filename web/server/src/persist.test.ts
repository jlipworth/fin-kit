import { describe, expect, test } from "bun:test";
import { resolveTable } from "./persist";
import { normalizeHistoryData, streamPatternToSqlLike } from "./trpc";

describe("resolveTable", () => {
  test("maps implied-rate calc streams for catch-all persistence", () => {
    expect(resolveTable("calc:implied_rate:TY")).toBe("calculated_implied_rates");
  });
});

describe("normalizeHistoryData", () => {
  test("parses JSON strings returned from postgres", () => {
    expect(normalizeHistoryData('{"price":"111.25","timestamp":"123"}')).toEqual({
      price: "111.25",
      timestamp: "123",
    });
  });

  test("passes through object data", () => {
    expect(normalizeHistoryData({ status: "ok" })).toEqual({ status: "ok" });
  });
});

describe("streamPatternToSqlLike", () => {
  test("converts all glob wildcards to SQL wildcards", () => {
    expect(streamPatternToSqlLike("market:*:*")).toBe("market:%:%");
  });

  test("leaves exact stream names unchanged", () => {
    expect(streamPatternToSqlLike("market:futures:TY")).toBe("market:futures:TY");
  });

  test("escapes LIKE metacharacters so underscores match literally", () => {
    expect(streamPatternToSqlLike("calc:implied_rate:TY")).toBe("calc:implied\\_rate:TY");
    expect(streamPatternToSqlLike("odd%name")).toBe("odd\\%name");
  });

  test("escapes metacharacters while translating wildcards", () => {
    expect(streamPatternToSqlLike("calc:implied_rate:*")).toBe("calc:implied\\_rate:%");
  });
});
