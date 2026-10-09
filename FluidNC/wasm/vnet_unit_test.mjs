// Usage: node FluidNC/wasm/vnet_unit_test.mjs
//
// Unit tests for the demo's virtual-network code that don't need the wasm
// module: demo/vnet-host.js's client core against a scripted fake module
// (connection timing and truncated responses the real server won't produce
// on demand), and demo/vnet-sw.js's findHost() against fake browser
// clients (recovering a frame's demo page after the worker restarts).
// Exits nonzero on the first failed check.

import { createRequire } from 'module';
import path from 'path';
import fs from 'fs';
import vm from 'vm';
const require = createRequire(import.meta.url);
const here = path.dirname(new URL(import.meta.url).pathname);
globalThis.self = globalThis;

let failures = 0;
function check(cond, what) {
  console.log((cond ? 'PASS ' : 'FAIL ') + what);
  if (!cond) {
    failures++;
  }
}
const tick = () => new Promise((r) => setTimeout(r, 0));

// ---- vnet-host.js client core against a fake module ----

const { createClient } = require(path.join(here, 'demo/vnet-host.js'));

function fakeModule() {
  const m = { openable: true, nextId: 1, opened: [], closed: [], sent: new Map() };
  m.cwrap = (name) => {
    if (name === 'fluidnc_vconn_open') {
      return () => {
        if (!m.openable) {
          return 0;
        }
        const id = m.nextId++;
        m.opened.push(id);
        return id;
      };
    }
    if (name === 'fluidnc_vconn_close') {
      return (id) => m.closed.push(id);
    }
    throw new Error('unexpected cwrap ' + name);
  };
  m.ccall = (name, ret, types, [id, bytes]) => {
    m.sent.set(id, (m.sent.get(id) || '') + Buffer.from(bytes).toString('latin1'));
  };
  m.reply = (id, text) => self.fluidncOnVconnData(id, new Uint8Array(Buffer.from(text, 'latin1')));
  m.hangUp = (id) => self.fluidncOnVconnClose(id);
  return m;
}

async function httpCase(response, hangUp = true) {
  const M = fakeModule();
  const c = createClient(M);
  const result = { data: '' };
  const done = new Promise((resolve) => {
    c.http({ method: 'GET', path: '/x' }, {
      head: (status) => (result.status = status),
      data: (b) => (result.data += Buffer.from(b).toString('latin1')),
      end: () => resolve(Object.assign(result, { outcome: 'end' })),
      error: (message) => resolve(Object.assign(result, { outcome: 'error', message })),
    });
  });
  await tick();
  const id = M.opened[0];
  M.reply(id, response);
  if (hangUp) {
    M.hangUp(id);
  }
  return Promise.race([done, new Promise((r) => setTimeout(() => r({ outcome: 'pending' }), 200))]);
}

let r = await httpCase('HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n12345');
check(r.outcome === 'error', 'Content-Length body cut short by close is an error, not success');
r = await httpCase('HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n');
check(r.outcome === 'error', 'chunked body closed before its last chunk is an error');
r = await httpCase('HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nall of it');
check(r.outcome === 'end' && r.data === 'all of it', 'close-delimited body ends successfully at close');
r = await httpCase('HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello', false);
check(r.outcome === 'end' && r.data === 'hello', 'complete Content-Length body ends without waiting for close');
r = await httpCase('HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n', false);
check(r.outcome === 'end' && r.data === 'hello', 'complete chunked body ends at its last chunk');
r = await httpCase('', true);
check(r.outcome === 'error' && /without a response/.test(r.message), 'close before any response reports no response');

{
  // A WebSocket closed while connect() is still waiting for the server must
  // not later open an upgrade.
  const M = fakeModule();
  M.openable = false;  // server not up yet
  const c = createClient(M);
  const events = [];
  const ws = c.websocket('/', [], {
    open: () => events.push('open'),
    text: () => {},
    binary: () => {},
    error: () => events.push('error'),
    close: (code) => events.push('close ' + code),
  });
  await tick();
  ws.close();
  M.openable = true;  // the server comes up; connect()'s retry succeeds
  await new Promise((resolve) => setTimeout(resolve, 250));
  const id = M.opened[0];
  check(id !== undefined && M.closed.includes(id), 'WebSocket closed during connect: late connection is closed');
  check(!M.sent.has(id), 'WebSocket closed during connect: no upgrade request is sent');
  check(events.length === 1 && events[0].startsWith('close'), 'WebSocket closed during connect: caller sees exactly one close');
}

// ---- vnet-sw.js findHost() after a worker restart ----

function fakeClient(id, url, onMessage) {
  return {
    id,
    url,
    postMessage(msg, ports) {
      const reply = onMessage && onMessage(msg);
      if (reply !== undefined && ports && ports[0]) {
        ports[0].postMessage(reply);
      }
    },
  };
}

function loadWorker(clients) {
  const ctx = {
    self: {
      registration: { scope: 'http://demo/device/' },
      location: { origin: 'http://demo' },
      addEventListener() {},
      clients: {
        get: async (id) => clients.find((c) => c.id === id) || undefined,
        matchAll: async () => clients.slice(),
      },
    },
    URL, MessageChannel, setTimeout, clearTimeout, Response, Headers, TransformStream,
    TextDecoderStream, TextEncoderStream, fetch,
  };
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(path.join(here, 'demo/vnet-sw.js'), 'utf8'), ctx);
  return ctx;
}

const hostA = fakeClient('A', 'http://demo/index.html', (m) => (m.type === 'vnet-who-has' ? { mine: m.token === 'tokA' } : undefined));
const hostB = fakeClient('B', 'http://demo/index.html', (m) => (m.type === 'vnet-who-has' ? { mine: m.token === 'tokB' } : undefined));
const frameB = fakeClient('F', 'http://demo/device/', (m) => (m.type === 'vnet-which-host' ? { token: 'tokB' } : undefined));
const silentFrame = fakeClient('S', 'http://demo/device/', () => undefined);

let w = loadWorker([hostA, hostB, frameB]);
let h = await w.findHost({ clientId: 'F' }, null);
check(h && h.id === 'B', 'restart, two demo tabs: frame and page are asked, request goes to the right tab');

w = loadWorker([hostA, hostB, silentFrame]);
h = await w.findHost({ clientId: 'S' }, null);
check(h === null, 'restart, two demo tabs, frame cannot be attributed: fails rather than guessing');

w = loadWorker([hostA, silentFrame]);
h = await w.findHost({ clientId: 'S' }, null);
check(h && h.id === 'A', 'restart, one demo tab: unattributable request goes to the only tab');

w = loadWorker([hostA, hostB]);
h = await w.findHost({ clientId: '', resultingClientId: 'N' }, 'tokA');
check(h && h.id === 'A', 'navigation carrying its token reaches the owning tab');

console.log(failures ? `\n${failures} check(s) FAILED` : '\nall checks passed');
process.exit(failures ? 1 : 0);
