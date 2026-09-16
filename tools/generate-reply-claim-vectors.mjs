#!/usr/bin/env node
/**
 * Generate tests/reply_claim_vectors.h from the shared reply-claim-vectors.json.
 *
 * The header used to be transcribed by hand from a file this repository did not
 * even carry, so a changed expectation upstream had nothing here to notice it.
 * Run with --check in CI to prove the committed header still matches.
 */
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const vectors = JSON.parse(readFileSync(join(root, "tests/fixtures/reply-claim-vectors.json"), "utf8"));

const stages = (row) => {
  const ids = [];
  for (const context of row.contexts) {
    const id = context.pipeline_id ?? context.pipeline;
    if (typeof id === "string" && id !== "" && !ids.includes(id)) ids.push(id);
  }
  return ids;
};

const widest = Math.max(4, ...vectors.cases.map((row) => stages(row).length));
const lines = vectors.cases.map((row) => {
  const ids = stages(row);
  const listed = ids.length ? ids.map((id) => JSON.stringify(id)).join(", ") : "NULL";
  return `    {${row.handled}, ${row.failed}, ${row.expected.claimed}, ${ids.length}, {${listed}}},`;
});

const header = `/* Generated from the shared Python reply-claim-vectors.json. */
/* Regenerate with tools/generate-reply-claim-vectors.mjs; do not edit. */
typedef struct { bool handled, failed, claimed; size_t count; const char *stages[${widest}]; } reply_claim_vector;
static const reply_claim_vector REPLY_CLAIM_VECTORS[] = {
${lines.join("\n")}
};
`;

const target = join(root, "tests/reply_claim_vectors.h");
if (process.argv.includes("--check")) {
  if (readFileSync(target, "utf8") !== header) {
    console.error("tests/reply_claim_vectors.h is out of date with the shared vectors; regenerate it.");
    process.exit(1);
  }
  console.log("reply claim vectors match the shared file");
} else {
  writeFileSync(target, header);
  console.log(`wrote ${target}`);
}
