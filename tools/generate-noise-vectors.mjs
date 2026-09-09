// Capture deterministic wire bytes from Thalovant Node's independent Noise
// implementation (@noble primitives), not the C implementation under test.
// Usage: node tools/generate-noise-vectors.mjs /path/to/built/thalovant-node-sdk
// Tested with Node SDK 0.3.3 (7bd30c source discovery), @noble 2.x.
// All keys/passwords below are public synthetic fixtures, never credentials.
import { resolve, join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { createRequire } from 'node:module';
import { readFile } from 'node:fs/promises';
const root = resolve(process.argv[2]);
const req = createRequire(join(root, 'package.json'));
// Load an isolated copy, replacing ONLY the ephemeral entropy source. The
// reference implementation is otherwise unchanged; source tree stays untouched.
const path = join(root, 'dist/src/noise.js');
let source = await readFile(path, 'utf8');
if (!source.includes('x25519.utils.randomSecretKey()')) throw new Error('unknown reference entropy call');
source = source.replaceAll('x25519.utils.randomSecretKey()', 'new Uint8Array(globalThis.__thalovantVectorEphemeral)');
source = source.replace(/from ["']([^"']+)["']/g, (_, spec) => `from ${JSON.stringify(pathToFileURL(spec.startsWith('.') ? resolve(root, 'dist/src', spec) : req.resolve(spec)).href)}`);
const n = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
const bytes = v => new Uint8Array(32).fill(v);
const hex = v => Buffer.from(v).toString('hex');
const utf8 = v => new TextEncoder().encode(v);
const staticI = bytes(0x11), staticR = bytes(0x22), eI = bytes(0x33), eR = bytes(0x44);
const hello = { node_id: 'fixture-hub', peer: 'test', pubkey: '', label: 'café/voice' };
const offer = { max_protocol_version: 3, binarize: true, encodings: ['JSON-HEX'], ciphers: ['AES-GCM'], noise: { patterns: ['XXpsk2','KKpsk0'], suites: ['25519_ChaChaPoly_SHA256', '25519_AESGCM_SHA256'] } };
const psk = n.derivePsk('fixture-password', hello.node_id);
const result = { source: 'Thalovant Node SDK Noise with @noble primitives; synthetic keys', password: 'fixture-password', node_id: hello.node_id, psk: hex(psk), hello, offer, static_i:hex(staticI),static_r:hex(staticR),ephemeral_i:hex(eI),ephemeral_r:hex(eR),public_i:hex(n.x25519PublicKey(staticI)),public_r:hex(n.x25519PublicKey(staticR)),exchanges:[] };
for (const suite of n.NOISE_SUITES) for (const pattern of ['XXpsk2','KKpsk0']) {
  const protocol = n.noiseProtocolName(pattern,suite), prologue = n.buildPrologue(hello,offer,protocol);
  const i = new n.NoiseHandshake(pattern,suite,psk,prologue,staticI,pattern==='KKpsk0'?n.x25519PublicKey(staticR):undefined);
  const r = new n.NoiseHandshake(pattern,suite,psk,prologue,staticR,pattern==='KKpsk0'?n.x25519PublicKey(staticI):undefined,false);
  const payloads = ['{"binarize":false,"encodings":[]}','{"accepted":true}',''];
  const messages = [];
  for(let step=0;step<(pattern==='XXpsk2'?3:2);step++) {
    globalThis.__thalovantVectorEphemeral = step%2===0?eI:eR;
    const sender=step%2===0?i:r,receiver=step%2===0?r:i;
    const message=sender.writeMessage(utf8(payloads[step]));
    if(hex(receiver.readMessage(message))!==hex(utf8(payloads[step])))throw new Error('peer payload mismatch');
    messages.push(hex(message));
  }
  const si=i.intoSession(),sr=r.intoSession();
  const plaintexts=['{"msg_type":"hello","payload":{}}','{"msg_type":"bus","payload":{"type":"test"}}','repeat'];
  const transport_i=plaintexts.map(p=>hex(si.encryptMessage(utf8(p),true)[0]));
  const transport_r=plaintexts.map(p=>hex(sr.encryptMessage(utf8(p),true)[0]));
  result.exchanges.push({pattern,suite,protocol,prologue:hex(prologue),payloads,messages,plaintexts,transport_i,transport_r});
}
process.stdout.write(JSON.stringify(result,null,2)+'\n');
