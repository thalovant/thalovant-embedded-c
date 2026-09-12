// Regenerate with: node tools/generate-request-fixtures.mjs ../thalovant-node-sdk
// The Node checkout must be built first. No production endpoints are contacted.
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
const source = process.argv[2];
if (!source) throw new Error('Pass a built Node SDK checkout.');
const sdk = await import(pathToFileURL(resolve(source, 'dist/src/index.js')));
const { SDK_VERSION } = await import(pathToFileURL(resolve(source, 'dist/src/version.js')));
const revision = execFileSync('git', ['-C', source, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim();
const patterns = ['[please] (repeat|say) {item_name}', 'did i (already |)ask (about|for|to|) {thing}', 'mute [it [for a bit]]', '[please]', '(a|b)', '(|a)', '{x} {x}', 'prefix {item_name}'];
const cString = value => JSON.stringify(value);
const lines = [`/* Generated from Node SDK ${SDK_VERSION}, commit ${revision}, by tools/generate-request-fixtures.mjs. */`,
  'static const char *const REQUEST_PATTERNS[] = {' + patterns.map(cString).join(',') + '};',
  'static const char *const REQUEST_SPOKEN[] = {' + patterns.map(p => cString(sdk.speakable(p, { item_name: 'the time' }))).join(',') + '};'];
await writeFile(new URL('../tests/fixtures/request-helpers-node.h', import.meta.url), lines.join('\n') + '\n');
