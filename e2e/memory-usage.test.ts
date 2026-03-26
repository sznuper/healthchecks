import { describe, it, expect } from "vitest";
import { runHealthcheck, parseEvents } from "./helpers.js";

describe("memory_usage", () => {
  describe("default", () => {
    it("produces correct output", async () => {
      const { stdout, exitCode } = await runHealthcheck("memory_usage");

      expect(exitCode).toBe(0);
      expect(stdout).toMatch(/^--- event$/m);

      const events = parseEvents(stdout);
      expect(events).toHaveLength(1);

      const e = events[0];
      expect(e.type).toMatch(/^(ok|high_usage|critical_usage)$/);
      expect(e.fields.usage_percent).toMatch(/^[0-9]/);
      expect(e.fields.total).toBeDefined();
      expect(e.fields.used).toBeDefined();
      expect(e.fields.available).toBeDefined();
      expect(e.fields.swap_total).toBeDefined();
      expect(e.fields.swap_used).toBeDefined();
      expect(e.fields.swap_usage_percent).toBeDefined();

      // No advanced fields in default mode
      expect(e.fields.free).toBeUndefined();
      expect(e.fields.buffers).toBeUndefined();
      expect(e.fields.cached).toBeUndefined();
    });
  });

  describe("threshold warn", () => {
    it("type is NOT ok with warn=0", async () => {
      const { stdout } = await runHealthcheck("memory_usage", {
        env: { HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT: "0" },
      });
      const events = parseEvents(stdout);
      expect(events[0].type).toMatch(/^(high_usage|critical_usage)$/);
      expect(events[0].type).not.toBe("ok");
    });
  });

  describe("threshold critical", () => {
    it("type is critical with both=0", async () => {
      const { stdout } = await runHealthcheck("memory_usage", {
        env: {
          HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT: "0",
          HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT: "0",
        },
      });
      const events = parseEvents(stdout);
      expect(events[0].type).toBe("critical_usage");
    });
  });

  describe("RAW mode", () => {
    it("byte fields are raw integers", async () => {
      const { stdout } = await runHealthcheck("memory_usage", {
        env: { HEALTHCHECK_ARG_RAW: "1" },
      });
      const e = parseEvents(stdout)[0];
      expect(e.fields.total).toMatch(/^[0-9]+$/);
      expect(e.fields.used).toMatch(/^[0-9]+$/);
      expect(e.fields.available).toMatch(/^[0-9]+$/);
      expect(e.fields.swap_total).toMatch(/^[0-9]+$/);
      expect(e.fields.swap_used).toMatch(/^[0-9]+$/);
    });
  });

  describe("ADVANCED mode", () => {
    it("includes extra fields", async () => {
      const { stdout } = await runHealthcheck("memory_usage", {
        env: { HEALTHCHECK_ARG_ADVANCED: "1" },
      });
      const e = parseEvents(stdout)[0];
      expect(e.fields.free).toBeDefined();
      expect(e.fields.buffers).toBeDefined();
      expect(e.fields.cached).toBeDefined();
      expect(e.fields.swap_free).toBeDefined();
      expect(e.fields.shared).toBeDefined();
      expect(e.fields.slab).toBeDefined();
    });
  });

  describe("RAW + ADVANCED", () => {
    it("byte fields are raw integers", async () => {
      const { stdout } = await runHealthcheck("memory_usage", {
        env: { HEALTHCHECK_ARG_RAW: "1", HEALTHCHECK_ARG_ADVANCED: "1" },
      });
      const e = parseEvents(stdout)[0];
      expect(e.fields.free).toMatch(/^[0-9]+$/);
      expect(e.fields.buffers).toMatch(/^[0-9]+$/);
      expect(e.fields.cached).toMatch(/^[0-9]+$/);
    });
  });
});
