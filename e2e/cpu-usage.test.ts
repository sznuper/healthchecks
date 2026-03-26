import { describe, it, expect } from "vitest";
import { runHealthcheck, parseEvents } from "./helpers.js";

// All cpu_usage tests use SAMPLE_MS=100 to avoid long sampling delays.
const FAST = { HEALTHCHECK_ARG_SAMPLE_MS: "100" };

describe("cpu_usage", () => {
  describe("default", () => {
    it("produces correct output", async () => {
      const { stdout, exitCode } = await runHealthcheck("cpu_usage", {
        env: FAST,
      });

      expect(exitCode).toBe(0);
      expect(stdout).toMatch(/^--- event$/m);

      const events = parseEvents(stdout);
      expect(events).toHaveLength(1);

      const e = events[0];
      expect(e.type).toMatch(/^(ok|high_usage|critical_usage)$/);
      expect(e.fields.usage_percent).toMatch(/^[0-9]/);
      expect(e.fields.user_percent).toBeDefined();
      expect(e.fields.system_percent).toBeDefined();
      expect(e.fields.idle_percent).toBeDefined();
      expect(e.fields.iowait_percent).toBeDefined();
      expect(e.fields.cores).toMatch(/^[0-9]/);

      // No advanced fields in default mode
      expect(e.fields.nice_percent).toBeUndefined();
      expect(e.fields.irq_percent).toBeUndefined();
      expect(e.fields.procs_running).toBeUndefined();
    });
  });

  describe("threshold warn", () => {
    it("type field present with warn=0", async () => {
      const { stdout } = await runHealthcheck("cpu_usage", {
        env: { ...FAST, HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT: "0" },
      });
      const events = parseEvents(stdout);
      expect(events[0].type).toMatch(/^(ok|high_usage|critical_usage)$/);
    });
  });

  describe("ADVANCED mode", () => {
    it("includes extra fields", async () => {
      const { stdout } = await runHealthcheck("cpu_usage", {
        env: { ...FAST, HEALTHCHECK_ARG_ADVANCED: "1" },
      });
      const e = parseEvents(stdout)[0];
      expect(e.fields.nice_percent).toBeDefined();
      expect(e.fields.irq_percent).toBeDefined();
      expect(e.fields.softirq_percent).toBeDefined();
      expect(e.fields.steal_percent).toBeDefined();
      expect(e.fields.guest_percent).toBeDefined();
      expect(e.fields.guest_nice_percent).toBeDefined();
      expect(e.fields.procs_running).toBeDefined();
      expect(e.fields.procs_blocked).toBeDefined();
    });
  });
});
