#!/usr/bin/env node
/**
 * Generate tests/refusal_vectors.h from the shared refusal-vectors.json.
 *
 * The cases are the Python reference's, so a changed expectation upstream has
 * something here to notice it. Each classification case becomes the bus frame
 * a hub actually sends, so the fields are read the way a client reads them --
 * through thalovant_ask_refusal -- rather than transcribed.
 *
 * Run with --check in CI to prove the committed header still matches.
 */
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const vectors = JSON.parse(readFileSync(join(root, "tests/fixtures/refusal-vectors.json"), "utf8"));

const cString = (value) => JSON.stringify(String(value));
const frameOf = (wire) =>
  JSON.stringify({ msg_type: "bus", payload: { type: wire.type, data: wire.data ?? {}, context: wire.context ?? {} } });

const refused = vectors.classification.filter((row) => row.expect.kind === "refused");
const unanswered = vectors.classification.filter((row) => row.expect.kind === "unanswered");
const widestAllowed = Math.max(1, ...refused.map((row) => row.expect.allowed.length));

const refusalLines = refused.map((row) => {
  const quota = row.expect.quota;
  const allowed = row.expect.allowed.map(cString);
  while (allowed.length < widestAllowed) allowed.push("NULL");
  return `    {${cString(row.name)}, ${cString(frameOf(row.event))}, ${cString(row.expect.denied_type)}, `
    + `${cString(row.expect.code)}, ${cString(row.expect.reason)}, `
    + `${quota ? "true" : "false"}, ${cString(quota?.period ?? "")}, ${quota?.limit ?? 0}, `
    + `${quota?.used ?? 0}, ${quota?.reset_after ?? 0}, ${row.expect.allowed.length}, {${allowed.join(", ")}}},`;
});

const unansweredLines = unanswered.map((row) => `    {${cString(row.name)}, ${cString(frameOf(row.event))}},`);

const correlationLines = vectors.correlation.map((row) =>
  `    {${cString(row.name)}, ${row.request_id === null ? "NULL" : cString(row.request_id === "own" ? "req-own" : "req-other")}, `
  + `${cString(row.denied_type)}, ${row.asks_in_flight}, ${row.queries_in_flight}, ${row.sends_in_flight}, ${row.taken}},`);

const header = `/* Generated from the shared Python refusal-vectors.json. */
/* Regenerate with tools/generate-refusal-vectors.mjs; do not edit. */
#define REFUSAL_GRACE_SECONDS ${vectors.untracked_grace_seconds}

/* A refusal, as the hub sends it, beside what every SDK must read out of it. */
typedef struct {
  const char *name;
  const char *frame;
  const char *denied_type;
  const char *code;
  const char *reason;
  bool has_quota;
  const char *period;
  long limit, used, reset_after;
  size_t allowed_count;
  const char *allowed[${widestAllowed}];
} refusal_vector;
static const refusal_vector REFUSAL_VECTORS[] = {
${refusalLines.join("\n")}
};

/* The hub understood and has nothing for it: not a refusal, not a fault. */
typedef struct { const char *name; const char *frame; } unanswered_vector;
static const unanswered_vector UNANSWERED_VECTORS[] = {
${unansweredLines.join("\n")}
};

/* Whose an uncorrelated denial is, when more than one utterance is out. */
typedef struct {
  const char *name;
  const char *request_id;
  const char *denied_type;
  size_t asks, queries, sends;
  bool taken;
} refusal_correlation_vector;
static const refusal_correlation_vector REFUSAL_CORRELATION_VECTORS[] = {
${correlationLines.join("\n")}
};
`;

const target = join(root, "tests/refusal_vectors.h");
if (process.argv.includes("--check")) {
  if (readFileSync(target, "utf8") !== header) {
    console.error("tests/refusal_vectors.h is out of date with the shared vectors; regenerate it.");
    process.exit(1);
  }
  console.log("refusal vectors match the shared file");
} else {
  writeFileSync(target, header);
  console.log(`wrote ${target}`);
}
