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
const source = readFileSync(join(root, "tests/fixtures/refusal-vectors.json"), "utf8");
const vectors = JSON.parse(source);

/*
 * The frames keep every number exactly as the shared file spells it. Through
 * JSON.parse and JSON.stringify a number is a double: `1e+20` came out as
 * `100000000000000000000` and `9223372036854775808` as `9223372036854776000`,
 * so the C reader was tested on a number the hub never sent. Each number
 * outside a string is parsed as a marker string carrying its own text, and the
 * marker is put back as that text once the frame is a string again.
 */
const NUMBER_MARK = "thalovant-number-lexeme:";
// Checked on the decoded values, not the text: a string can spell the marker
// with JSON escapes and still decode to it.
if (JSON.stringify(vectors).includes(NUMBER_MARK)) throw new Error(`the shared vectors spell ${NUMBER_MARK} themselves`);
function withNumbersAsText(text) {
  let out = "";
  for (let at = 0; at < text.length;) {
    const char = text[at];
    if (char === '"') {
      let end = at + 1;
      while (text[end] !== '"') end += text[end] === "\\" ? 2 : 1;
      out += text.slice(at, end + 1);
      at = end + 1;
    } else if (char === "-" || (char >= "0" && char <= "9")) {
      const lexeme = /^-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?/.exec(text.slice(at))[0];
      out += JSON.stringify(NUMBER_MARK + lexeme);
      at += lexeme.length;
    } else {
      out += char;
      at += 1;
    }
  }
  return out;
}
const lexical = JSON.parse(withNumbersAsText(source));
const markedNumber = new RegExp(`"${NUMBER_MARK}([^"]+)"`, "g");

const cString = (value) => JSON.stringify(String(value));
const frameOf = (wire) =>
  JSON.stringify({ msg_type: "bus", payload: { type: wire.type, data: wire.data ?? {}, context: wire.context ?? {} } })
    .replace(markedNumber, "$1");

// The expectations are read as numbers; the frames come from the same rows
// with every number still spelled as the shared file spells it.
const rows = vectors.classification.map((row, index) => ({ ...row, event: lexical.classification[index].event }));
const refused = rows.filter((row) => row.expect.kind === "refused");
const unanswered = rows.filter((row) => row.expect.kind === "unanswered");
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
#include <stdint.h>

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
  int64_t limit, used, reset_after;
  size_t allowed_count;
  const char *allowed[${widestAllowed}];
} refusal_vector;
static const refusal_vector REFUSAL_VECTORS[] = {
${refusalLines.join("\n")}
};

/* The hub understood and has nothing for it: a failure event, but never a refusal. */
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
