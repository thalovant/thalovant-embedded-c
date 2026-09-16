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

const firstSeen = (row, pick) => {
  const ids = [];
  for (const context of row.contexts) {
    const id = pick(context);
    if (typeof id === "string" && id !== "" && !ids.includes(id)) ids.push(id);
  }
  return ids;
};

const stages = (row) => firstSeen(row, (c) => c.pipeline_id ?? c.pipeline);
const skills = (row) => firstSeen(row, (c) => c.skill_id ?? c.skill);

// What this derives from `contexts` has to match what the shared file says it
// should be. Without this the header could be generated from a reading of the
// contexts that disagrees with `expected.pipeline_ids`, and nothing downstream
// would notice: the C test reads only `claimed`.
for (const row of vectors.cases) {
  const derived = stages(row);
  const expected = row.expected.pipeline_ids ?? [];
  if (JSON.stringify(derived) !== JSON.stringify(expected)) {
    console.error(
      `${row.name}: derived pipeline ids ${JSON.stringify(derived)} do not match ` +
        `expected ${JSON.stringify(expected)}`,
    );
    process.exit(1);
  }
  // The C test asserts pipeline ids only, so `expected.skill_ids` could be
  // edited to anything and still pass `make test`. It is shared data with the
  // same first-seen, unique-order rule, so check it here rather than leave a
  // field in the fixture that nothing polices.
  if (row.expected.skill_ids !== undefined) {
    const derivedSkills = skills(row);
    if (JSON.stringify(derivedSkills) !== JSON.stringify(row.expected.skill_ids)) {
      console.error(
        `${row.name}: derived skill ids ${JSON.stringify(derivedSkills)} do not match ` +
          `expected ${JSON.stringify(row.expected.skill_ids)}`,
      );
      process.exit(1);
    }
  }
}

// A frame per context, so the C side can run thalovant_ask_event_skill_id over
// the shared cases instead of one hand-written frame. Without this the header
// carried pipeline ids only, and `expected.skill_ids` could be edited to
// anything without a C test noticing.
const frameFor = (context) => JSON.stringify(JSON.stringify({
  msg_type: "bus",
  payload: {
    type: "speak",
    data: { utterance: "x" },
    context: { request_id: "r", ...context },
  },
}));

const skillCases = vectors.cases.filter((row) => Array.isArray(row.expected.skill_ids));
const widestSkillFrames = Math.max(1, ...skillCases.map((row) => row.contexts.length));
const widestSkills = Math.max(1, ...skillCases.map((row) => skills(row).length));
const skillLines = skillCases.map((row) => {
  const frames = row.contexts.map(frameFor).join(", ");
  const ids = skills(row);
  const listed = ids.length ? ids.map((id) => JSON.stringify(id)).join(", ") : "NULL";
  return `    {${row.contexts.length}, {${frames || "NULL"}}, ${ids.length}, {${listed}}},`;
});

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

/* One bus frame per context, so the skill ids are read the way a client reads
   them -- through thalovant_ask_event_skill_id -- rather than transcribed. */
typedef struct {
  size_t frames;
  const char *frame[${widestSkillFrames}];
  size_t count;
  const char *skills[${widestSkills}];
} reply_skill_vector;
static const reply_skill_vector REPLY_SKILL_VECTORS[] = {
${skillLines.join("\n")}
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
