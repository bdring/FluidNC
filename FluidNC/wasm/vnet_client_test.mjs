// Usage: node FluidNC/wasm/vnet_client_test.mjs .pio/build/wasm/program.js
//
// Drives demo/vnet-host.js's client core -- the same HTTP/WebSocket client
// the browser demo uses -- against FluidNC's real web server in the wasm
// build, headlessly.  Exits nonzero on the first failed check.

import { createRequire } from 'module';
import path from 'path';
import zlib from 'zlib';
const require = createRequire(import.meta.url);
globalThis.self = globalThis;

const FluidNCModule = require(path.resolve(process.argv[2]));
const { createClient } = require(path.resolve(path.dirname(new URL(import.meta.url).pathname), 'demo/vnet-host.js'));

const CONFIG = `name: "vnet client test"
board: "None"
stepping:
  engine: timed
axes:
  x:
    steps_per_mm: 800
    max_rate_mm_per_min: 3000
    acceleration_mm_per_sec2: 100
    max_travel_mm: 300
    motor0:
      standard_stepper:
        step_pin: gpio.10
        direction_pin: gpio.11
start:
  must_home: false
`;
const INDEX = '<!doctype html><html><head><title>t</title></head><body>gz index</body></html>';

self.fluidncOnOutput = () => {};

let failures = 0;
function check(cond, what) {
  console.log((cond ? 'PASS ' : 'FAIL ') + what);
  if (!cond) {
    failures++;
  }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Emscripten's pthread workers are unref'd, so nothing else keeps Node alive
// while a request is in flight.
setInterval(() => {}, 1000);

const M = await FluidNCModule();
// Files go straight into the module's filesystem before FluidNC starts,
// as the demo page seeds them.
const stage = (root, name, content) => {
  M.FS.mkdirTree('/' + root);
  M.FS.writeFile('/' + root + '/' + name, content);
};
stage('native_localfs', 'config.yaml', CONFIG);
M.FS.writeFile('native_localfs/index.html.gz', zlib.gzipSync(INDEX));
M.cwrap('fluidnc_start', null, [])();

const client = createClient(M);

function get(method, p, { headers = [], body = null } = {}) {
  return new Promise((resolve) => {
    const res = { status: 0, headers: [], chunks: [] };
    client.http({ method, path: p, headers, body }, {
      head: (status, statusText, h) => Object.assign(res, { status, statusText, headers: h }),
      data: (b) => res.chunks.push(Buffer.from(b)),
      end: () => resolve({ ...res, body: Buffer.concat(res.chunks) }),
      error: (message) => resolve({ ...res, error: message }),
    });
  });
}
const hdr = (r, n) => (r.headers.find(([k]) => k.toLowerCase() === n) || [])[1];

// 1. GET / -> index.html.gz served gzip-encoded, Set-Cookie captured by the jar
let r = await get('GET', '/');
check(r.status === 200, 'GET / status 200');
check(/gzip/.test(hdr(r, 'content-encoding') || ''), 'GET / is gzip-encoded');
check(zlib.gunzipSync(r.body).toString() === INDEX, 'GET / body gunzips to index.html');
check(client.cookieList().some((c) => c.name === 'sessionId' && c.path === '/'), 'sessionId cookie stored in jar for path /');
check(!hdr(r, 'set-cookie'), 'Set-Cookie stripped from forwarded headers');

// 2. Chunked response: [ESP800]
r = await get('GET', '/command?plain=' + encodeURIComponent('[ESP800]json=yes'));
let fw = null;
try {
  fw = JSON.parse(r.body.toString());
} catch {}
check(r.status === 200 && fw && fw.data.WebSocketPort === '80', 'ESP800 over chunked encoding parses, WebSocketPort 80');
check(!hdr(r, 'transfer-encoding'), 'Transfer-Encoding stripped after de-chunking');

// 3. Upload with the size field, then read it back
const content = 'G0 X5\n'.repeat(5000);  // 30 KB: several send chunks
const B = 'vnetboundary';
const mp = Buffer.concat([
  Buffer.from(`--${B}\r\nContent-Disposition: form-data; name="path"\r\n\r\n/\r\n`),
  Buffer.from(`--${B}\r\nContent-Disposition: form-data; name="/big.nc S"\r\n\r\n${content.length}\r\n`),
  Buffer.from(`--${B}\r\nContent-Disposition: form-data; name="myfile[]"; filename="/big.nc"\r\nContent-Type: application/octet-stream\r\n\r\n`),
  Buffer.from(content),
  Buffer.from(`\r\n--${B}--\r\n`),
]);
r = await get('POST', '/files', { headers: [['Content-Type', 'multipart/form-data; boundary=' + B]], body: mp });
let list = null;
try {
  list = JSON.parse(r.body.toString());
} catch {}
check(r.status === 200 && list && list.status === 'Ok' && list.files.some((f) => f.name === 'big.nc' && Number(f.size) === content.length),
  'multipart upload of 30 KB file succeeds with size check');
r = await get('GET', '/big.nc');
check(r.body.toString() === content, 'uploaded file reads back intact');

// 4. 404 with Content-Length body
r = await get('GET', '/nope.txt');
check(r.status === 404 && r.body.length === Number(hdr(r, 'content-length')), '404 body length matches Content-Length');

// 4b. Install sequence used by the demo's "Install WebUI" (index.html):
// list, delete index.html / index.html.gz, upload index.html.gz with its
// size field first, verify -- over a stale plain index.html, which FluidNC
// would otherwise keep serving in preference to the new .gz.
{
  M.FS.writeFile('native_localfs/index.html', 'stale plain index');
  const NEW = '<!doctype html><html><head></head><body>installed ui</body></html>';
  const gz = zlib.gzipSync(NEW);
  const listing = JSON.parse((await get('GET', '/files?path=/')).body.toString());
  const existing = listing.files.filter((f) => f.name === 'index.html' || f.name === 'index.html.gz').map((f) => f.name);
  check(existing.includes('index.html') && existing.includes('index.html.gz'), 'install: both index variants listed before install');
  for (const name of existing) {
    const del = await get('GET', '/files?path=/&action=delete&dontlist=yes&filename=' + encodeURIComponent(name));
    // The installer treats anything else as a failed delete.
    check(JSON.parse(del.body.toString()).status === name + ' deleted', `install: delete of ${name} reports "${name} deleted"`);
  }
  const b = 'installboundary';
  const body = Buffer.concat([
    Buffer.from(`--${b}\r\nContent-Disposition: form-data; name="path"\r\n\r\n/\r\n`),
    Buffer.from(`--${b}\r\nContent-Disposition: form-data; name="/index.html.gzS"\r\n\r\n${gz.length}\r\n`),
    Buffer.from(`--${b}\r\nContent-Disposition: form-data; name="myfile[]"; filename="/index.html.gz"\r\nContent-Type: application/octet-stream\r\n\r\n`),
    gz,
    Buffer.from(`\r\n--${b}--\r\n`),
  ]);
  const up = await get('POST', '/files', { headers: [['Content-Type', 'multipart/form-data; boundary=' + b]], body });
  const after = JSON.parse(up.body.toString());
  const inst = after.files.find((f) => f.name === 'index.html.gz');
  check(after.status === 'Ok' && inst && Number(inst.size) === gz.length && !after.files.some((f) => f.name === 'index.html'),
    'install: index.html removed, index.html.gz uploaded with matching size');
  const root = await get('GET', '/');
  check(zlib.gunzipSync(root.body).toString() === NEW, 'install: GET / now serves the installed WebUI');
}

// 5. WebSocket session
const events = [];
let opened;
const openP = new Promise((res) => (opened = res));
let closedP;
const closed = new Promise((res) => (closedP = res));
const ws = client.websocket('/', ['arduino'], {
  open: (protocol) => {
    events.push(['open', protocol]);
    opened();
  },
  text: (s) => events.push(['text', s]),
  binary: (ab) => events.push(['binary', Buffer.from(ab).toString()]),
  error: () => events.push(['error']),
  close: (code, reason, clean) => {
    events.push(['close', code, clean]);
    closedP();
  },
});
ws.sendText('?');  // queued until the handshake completes
await Promise.race([openP, sleep(5000)]);
check(events[0] && events[0][0] === 'open' && events[0][1] === 'arduino', 'WebSocket opens with subprotocol arduino');
await sleep(300);
ws.sendText('$I\n');
await sleep(800);
const texts = events.filter((e) => e[0] === 'text').map((e) => e[1]);
const bin = events.filter((e) => e[0] === 'binary').map((e) => e[1]).join('');
check(texts.some((t) => /^currentID:\d+$/.test(t)), 'currentID text frame received');
check(/<Idle\|MPos:/.test(bin), 'status report received (queued pre-open send delivered)');
check(/\[VER:.*FluidNC/.test(bin) && /\nok\n/.test(bin), '$I answered with [VER:] ... ok');
ws.close(1000, 'bye');
await Promise.race([closed, sleep(3000)]);
const c = events.find((e) => e[0] === 'close');
check(c && c[1] === 1000 && c[2] === true, 'WebSocket closes cleanly with code 1000');

console.log(failures ? `\n${failures} check(s) FAILED` : '\nall checks passed');
process.exit(failures ? 1 : 0);
