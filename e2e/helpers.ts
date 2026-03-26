import { execa } from "execa";
import path from "node:path";

export const BUILD_DIR =
  process.env.BUILD_DIR || path.resolve(import.meta.dirname, "../build");

/**
 * Run a healthcheck binary by name with optional env vars and stdin.
 */
export async function runHealthcheck(
  name: string,
  opts?: { env?: Record<string, string>; input?: string },
) {
  const binPath = path.join(BUILD_DIR, name);
  const result = await execa(binPath, [], {
    reject: false,
    env: opts?.env,
    input: opts?.input,
    timeout: 10_000,
  });
  return {
    stdout: result.stdout,
    stderr: result.stderr,
    exitCode: result.exitCode,
  };
}

/**
 * Parse `--- event` delimited output into an array of event objects.
 */
export function parseEvents(
  stdout: string,
): Array<{ type: string; fields: Record<string, string> }> {
  const blocks = stdout.split("--- event\n").filter((b) => b.trim() !== "");
  return blocks.map((block) => {
    const fields: Record<string, string> = {};
    for (const line of block.split("\n")) {
      const idx = line.indexOf("=");
      if (idx === -1) continue;
      fields[line.slice(0, idx)] = line.slice(idx + 1);
    }
    return { type: fields.type || "", fields };
  });
}

/**
 * Count occurrences of `--- event` in output.
 */
export function countEvents(stdout: string): number {
  return (stdout.match(/^--- event$/gm) || []).length;
}
