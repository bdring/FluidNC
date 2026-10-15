// Usage: node FluidNC/wasm/vconn_smoke_test.mjs .pio/build/wasm/program.js
// Headless smoke test of BrowserAsyncTCP: raw HTTP over fluidnc_vconn_*.
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
globalThis.self = globalThis;
const FluidNCModule = require(require("path").resolve(process.argv[2]));

const CONFIG = `name: "vconn test"
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

let consoleText = '';
self.fluidncOnOutput = (t) => { consoleText += t; };

const conns = new Map();   // id -> {chunks:[], resolve}
self.fluidncOnVconnData = (id, bytes) => { conns.get(id)?.chunks.push(Buffer.from(bytes)); };
self.fluidncOnVconnClose = (id) => { const c = conns.get(id); if (c) { conns.delete(id); c.resolve(Buffer.concat(c.chunks).toString('latin1')); } };

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

const M = await FluidNCModule();
const start = M.cwrap('fluidnc_start', null, []);
// Files go straight into the module's filesystem before FluidNC starts,
// as the demo page seeds them.
const stage = (root, name, content) => {
  M.FS.mkdirTree('/' + root);
  M.FS.writeFile('/' + root + '/' + name, content);
};
const open = M.cwrap('fluidnc_vconn_open', 'number', ['number']);
const send = (id, s) => M.ccall('fluidnc_vconn_send', null, ['number', 'array', 'number'], [id, Buffer.from(s, 'latin1'), s.length]);

stage('native_localfs', 'config.yaml', CONFIG);
stage('native_localfs', 'index.html', '<html><body>hello from localfs</body></html>');
start();

let id = 0;
for (let i = 0; i < 100 && !id; i++) { await sleep(100); id = open(80); }
console.log('--- boot console (tail) ---\n' + consoleText.split('\n').filter(l => /HTTP|Virtual|ERR|error/i.test(l)).join('\n'));
if (!id) { console.log('FAIL: no server on port 80'); process.exit(1); }

async function request(req, timeoutMs = 3000) {
  const cid = open(80);
  const p = new Promise(resolve => conns.set(cid, { chunks: [], resolve }));
  send(cid, req);
  const r = await Promise.race([p, sleep(timeoutMs).then(() => 'TIMEOUT (partial): ' + Buffer.concat(conns.get(cid)?.chunks ?? []).toString('latin1'))]);
  return r;
}


for (const path of ['/', '/index.html', '/command?plain=%5BESP800%5Djson%3Dyes']) {
  const r = await request(`GET ${path} HTTP/1.1\r\nHost: fluidnc\r\nConnection: close\r\n\r\n`);
  console.log(`\n=== GET ${path} ===\n` + r.slice(0, 400));
}

// Multipart upload to LocalFS, with the <filename>S size field WebUI sends.
{
  const body = 'G0 X1\nG0 X0\n';
  const B = 'XyZboundary';
  const mp = `--${B}\r\nContent-Disposition: form-data; name="path"\r\n\r\n/\r\n` +
             `--${B}\r\nContent-Disposition: form-data; name="/up.nc S"\r\n\r\n${body.length}\r\n` +
             `--${B}\r\nContent-Disposition: form-data; name="myfile[]"; filename="/up.nc"\r\nContent-Type: application/octet-stream\r\n\r\n${body}\r\n` +
             `--${B}--\r\n`;
  const r = await request(`POST /files HTTP/1.1\r\nHost: fluidnc\r\nConnection: close\r\nContent-Type: multipart/form-data; boundary=${B}\r\nContent-Length: ${mp.length}\r\n\r\n${mp}`);
  console.log('\n=== POST /files (upload /up.nc) ===\n' + r.slice(0, 600));
  const r2 = await request(`GET /up.nc HTTP/1.1\r\nHost: fluidnc\r\nConnection: close\r\n\r\n`);
  console.log('\n=== GET /up.nc ===\n' + r2);
}

// WebSocket: upgrade, read currentID, send "?" and "$I\n".
{
  const cid = open(80);
  let buf = Buffer.alloc(0);
  conns.set(cid, { chunks: { push: (b) => { buf = Buffer.concat([buf, b]); } }, resolve: () => console.log('(ws closed)') });
  send(cid, 'GET / HTTP/1.1\r\nHost: fluidnc\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
            'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: arduino\r\n\r\n');
  await sleep(500);
  const hdrEnd = buf.indexOf('\r\n\r\n');
  console.log('\n=== WS handshake ===\n' + buf.slice(0, hdrEnd).toString());
  buf = buf.slice(hdrEnd + 4);
  const frame = (text) => {               // masked client text frame, len < 126
    const p = Buffer.from(text), mask = Buffer.from([1, 2, 3, 4]);
    const f = Buffer.alloc(6 + p.length);
    f[0] = 0x81; f[1] = 0x80 | p.length; mask.copy(f, 2);
    for (let i = 0; i < p.length; i++) f[6 + i] = p[i] ^ mask[i & 3];
    return f;
  };
  const sendBin = (b) => M.ccall('fluidnc_vconn_send', null, ['number', 'array', 'number'], [cid, b, b.length]);
  sendBin(frame('?'));
  sendBin(frame('$I\n'));
  await sleep(800);
  // decode server frames (unmasked)
  let off = 0;
  console.log('=== WS frames ===');
  while (off + 2 <= buf.length) {
    const op = buf[off] & 0x0f; let len = buf[off + 1] & 0x7f; let h = 2;
    if (len === 126) { len = buf.readUInt16BE(off + 2); h = 4; }
    if (off + h + len > buf.length) break;
    console.log((op === 1 ? 'text  ' : op === 2 ? 'binary' : 'op' + op) + ' ' + JSON.stringify(buf.slice(off + h, off + h + len).toString()));
    off += h + len;
  }
}
process.exit(0);
