import { describe, it, expect } from "vitest";
import { runHealthcheck, parseEvents, countEvents } from "./helpers.js";

describe("ssh_journal", () => {
  describe("mixed events", () => {
    it("parses failure, login, and logout from mixed input", async () => {
      const input = [
        '{"MESSAGE":"Invalid user admin from 1.2.3.4 port 55000","__REALTIME_TIMESTAMP":"1772539305000000"}',
        '{"MESSAGE":"Server listening on 0.0.0.0 port 22","__REALTIME_TIMESTAMP":"1772539306000000"}',
        '{"MESSAGE":"Accepted publickey for deploy from 192.168.1.100 port 44222 ssh2","__REALTIME_TIMESTAMP":"1772539307000000"}',
        '{"MESSAGE":"Connection closed by invalid user test 1.2.3.4 port 55000 [preauth]","__REALTIME_TIMESTAMP":"1772539308000000"}',
        '{"MESSAGE":"Disconnected from user deploy 192.168.1.100 port 44222","__REALTIME_TIMESTAMP":"1772539309000000"}',
      ].join("\n");

      const { stdout } = await runHealthcheck("ssh_journal", { input });
      const events = parseEvents(stdout);

      expect(events).toHaveLength(3);

      const types = events.map((e) => e.type);
      expect(types).toContain("failure");
      expect(types).toContain("login");
      expect(types).toContain("logout");

      // All events should have user, host, timestamp
      for (const e of events) {
        expect(e.fields.user).toBeDefined();
        expect(e.fields.host).toBeDefined();
        expect(e.fields.timestamp).toMatch(
          /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/,
        );
      }
    });
  });

  describe("empty input", () => {
    it("produces zero events", async () => {
      const { stdout } = await runHealthcheck("ssh_journal", { input: "" });
      expect(countEvents(stdout)).toBe(0);
    });
  });

  describe("no matching messages", () => {
    it("produces zero events for non-SSH messages", async () => {
      const input =
        '{"MESSAGE":"Server listening on 0.0.0.0 port 22","__REALTIME_TIMESTAMP":"123"}';
      const { stdout } = await runHealthcheck("ssh_journal", { input });
      expect(countEvents(stdout)).toBe(0);
    });
  });

  describe("non-JSON input", () => {
    it("skips non-JSON lines", async () => {
      const input = [
        "not json",
        '{"MESSAGE":"Invalid user root from 1.1.1.1 port 22","__REALTIME_TIMESTAMP":"1000000"}',
      ].join("\n");
      const { stdout } = await runHealthcheck("ssh_journal", { input });
      expect(countEvents(stdout)).toBe(1);
    });
  });

  describe("all failure types", () => {
    it("handles Invalid user and Connection closed", async () => {
      const input = [
        '{"MESSAGE":"Invalid user admin from 1.2.3.4 port 55000","__REALTIME_TIMESTAMP":"1000000"}',
        '{"MESSAGE":"Connection closed by authenticating user root 10.0.0.1 port 22 [preauth]","__REALTIME_TIMESTAMP":"2000000"}',
      ].join("\n");
      const { stdout } = await runHealthcheck("ssh_journal", { input });
      const events = parseEvents(stdout);
      expect(events.filter((e) => e.type === "failure")).toHaveLength(2);
    });
  });

  describe("all login types", () => {
    it("handles publickey and password logins", async () => {
      const input = [
        '{"MESSAGE":"Accepted publickey for alice from 10.0.0.1 port 22 ssh2","__REALTIME_TIMESTAMP":"1000000"}',
        '{"MESSAGE":"Accepted password for bob from 10.0.0.2 port 22 ssh2","__REALTIME_TIMESTAMP":"2000000"}',
      ].join("\n");
      const { stdout } = await runHealthcheck("ssh_journal", { input });
      const events = parseEvents(stdout);
      expect(events.filter((e) => e.type === "login")).toHaveLength(2);
    });
  });

  describe("logout events", () => {
    it("handles Disconnected from user", async () => {
      const input = [
        '{"MESSAGE":"Disconnected from user deploy 192.168.1.100 port 44222","__REALTIME_TIMESTAMP":"1000000"}',
        '{"MESSAGE":"Disconnected from user root 10.0.0.1 port 22","__REALTIME_TIMESTAMP":"2000000"}',
      ].join("\n");
      const { stdout } = await runHealthcheck("ssh_journal", { input });
      const events = parseEvents(stdout);
      expect(events.filter((e) => e.type === "logout")).toHaveLength(2);
    });
  });

  describe("ADVANCED mode", () => {
    it("emits extra JSON fields, excludes MESSAGE and __REALTIME_TIMESTAMP", async () => {
      const input =
        '{"MESSAGE":"Invalid user admin from 1.2.3.4 port 55000","__REALTIME_TIMESTAMP":"1000000","_HOSTNAME":"vps"}';
      const { stdout } = await runHealthcheck("ssh_journal", {
        input,
        env: { HEALTHCHECK_ARG_ADVANCED: "1" },
      });
      const e = parseEvents(stdout)[0];
      expect(e.fields._HOSTNAME).toBe("vps");
      expect(e.fields.MESSAGE).toBeUndefined();
      expect(e.fields.__REALTIME_TIMESTAMP).toBeUndefined();
    });
  });
});
