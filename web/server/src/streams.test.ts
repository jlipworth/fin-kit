import { describe, expect, test } from "bun:test";
import { matchesPattern } from "./ws";

describe("matchesPattern", () => {
  test("exact match", () => {
    expect(matchesPattern("market:futures:TY", "market:futures:TY")).toBe(true);
  });

  test("wildcard at end", () => {
    expect(matchesPattern("market:futures:TY", "market:futures:*")).toBe(true);
    expect(matchesPattern("market:futures:US", "market:futures:*")).toBe(true);
  });

  test("wildcard does not match different prefix", () => {
    expect(matchesPattern("calc:basis:bond:X", "market:futures:*")).toBe(false);
  });

  test("wildcard in middle", () => {
    expect(matchesPattern("market:rates:ois:1Y", "market:rates:*")).toBe(true);
  });

  test("heartbeat pattern", () => {
    expect(matchesPattern("heartbeat:finkit-stream", "heartbeat:*")).toBe(true);
    expect(matchesPattern("heartbeat:mock-publisher", "heartbeat:*")).toBe(true);
  });

  test("subscribe-all pattern", () => {
    expect(matchesPattern("market:futures:TY", "*")).toBe(true);
    expect(matchesPattern("calc:curves:sofr", "*")).toBe(true);
  });
});
