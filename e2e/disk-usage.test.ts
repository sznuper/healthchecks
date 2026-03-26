import { describe, it, expect } from "vitest";
import { runHealthcheck, parseEvents, countEvents } from "./helpers.js";

describe("disk_usage", () => {
  describe("default", () => {
    it("produces correct output", async () => {
      const { stdout, exitCode } = await runHealthcheck("disk_usage");

      expect(exitCode).toBe(0);
      expect(stdout).toMatch(/^--- event$/m);

      const events = parseEvents(stdout);
      expect(events).toHaveLength(1);

      const e = events[0];
      expect(e.type).toMatch(/^(ok|high_usage|critical_usage)$/);
      expect(e.fields.mount).toMatch(/^\//);
      expect(e.fields.usage_percent).toMatch(/^[0-9]/);
      expect(e.fields.total).toBeDefined();
      expect(e.fields.used).toBeDefined();
      expect(e.fields.available).toBeDefined();

      // No advanced fields in default mode
      expect(e.fields.free).toBeUndefined();
      expect(e.fields.inodes).toBeUndefined();
    });
  });

  describe("threshold warn", () => {
    it("type is NOT ok with warn=0", async () => {
      const { stdout } = await runHealthcheck("disk_usage", {
        env: { HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT: "0" },
      });
      const events = parseEvents(stdout);
      expect(events[0].type).toMatch(/^(high_usage|critical_usage)$/);
      expect(events[0].type).not.toBe("ok");
    });
  });

  describe("threshold critical", () => {
    it("type is critical with both=0", async () => {
      const { stdout } = await runHealthcheck("disk_usage", {
        env: {
          HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT: "0",
          HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT: "0",
        },
      });
      const events = parseEvents(stdout);
      expect(events[0].type).toBe("critical_usage");
    });
  });

  describe("custom mount", () => {
    it("mount=/tmp when set", async () => {
      const { stdout } = await runHealthcheck("disk_usage", {
        env: { HEALTHCHECK_ARG_MOUNT: "/tmp" },
      });
      const events = parseEvents(stdout);
      expect(events[0].fields.mount).toBe("/tmp");
    });
  });

  describe("RAW mode", () => {
    it("byte fields are raw integers", async () => {
      const { stdout } = await runHealthcheck("disk_usage", {
        env: { HEALTHCHECK_ARG_RAW: "1" },
      });
      const e = parseEvents(stdout)[0];
      expect(e.fields.total).toMatch(/^[0-9]+$/);
      expect(e.fields.used).toMatch(/^[0-9]+$/);
      expect(e.fields.available).toMatch(/^[0-9]+$/);
    });
  });

  describe("ADVANCED mode", () => {
    it("includes extra fields", async () => {
      const { stdout } = await runHealthcheck("disk_usage", {
        env: { HEALTHCHECK_ARG_ADVANCED: "1" },
      });
      const e = parseEvents(stdout)[0];
      expect(e.fields.free).toBeDefined();
      expect(e.fields.inodes).toBeDefined();
      expect(e.fields.inodes_used).toBeDefined();
      expect(e.fields.inodes_free).toBeDefined();
      expect(e.fields.inodes_available).toBeDefined();
      expect(e.fields.inodes_usage_percent).toBeDefined();
    });
  });

  describe("RAW + ADVANCED", () => {
    it("free is raw integer", async () => {
      const { stdout } = await runHealthcheck("disk_usage", {
        env: { HEALTHCHECK_ARG_RAW: "1", HEALTHCHECK_ARG_ADVANCED: "1" },
      });
      const e = parseEvents(stdout)[0];
      expect(e.fields.free).toMatch(/^[0-9]+$/);
    });
  });
});
