import { describe, expect, test } from "bun:test";
import { resolveTable } from "./persist";
import { normalizeHistoryData } from "./trpc";

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
