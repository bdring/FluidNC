// Host-page side of the wasm demo's virtual network: an HTTP/1.1 and
// WebSocket *client* that talks to FluidNC's own web server (WebUI/ built
// into the wasm build) over the fluidnc_vconn_* exports of BrowserAsyncTCP.
//
// Two kinds of traffic arrive here:
// - HTTP requests from vnet-sw.js (the Service Worker controlling the device
//   iframe), as {type:'vnet-http', method, path, headers, body} plus a
//   MessagePort for the streamed reply.
// - WebSocket opens from vnet-ws-shim.js inside the iframe, as
//   {type:'vnet-ws-open', path, protocols} plus a MessagePort that then
//   carries the socket's traffic in both directions.
//
// Each becomes one virtual connection: the raw bytes FluidNC would see on a
// TCP socket go in, and the raw bytes it writes come back and are parsed
// here.  Doing the protocol work in one place, next to the wasm instance,
// also lets it keep the cookie jar: browsers drop Set-Cookie from responses
// a Service Worker constructs, so FluidNC's sessionId cookie is remembered
// here and sent on later requests and WebSocket upgrades.
//
// The client core (createClient) has no DOM dependencies, so it can be
// driven from Node for testing; attach() adds the browser wiring.

(function (global) {
  'use strict';

  const HTTP_PORT = 80;
  const SEND_CHUNK = 8192;  // bytes per fluidnc_vconn_send call (ccall copies via the stack)
  const utf8 = new TextEncoder();
  const utf8dec = new TextDecoder();

  function latin1(bytes) {
    let s = '';
    for (let i = 0; i < bytes.length; i++) {
      s += String.fromCharCode(bytes[i]);
    }
    return s;
  }

  function concat(a, b) {
    if (!a.length) {
      return b;
    }
    const out = new Uint8Array(a.length + b.length);
    out.set(a, 0);
    out.set(b, a.length);
    return out;
  }

  function indexOfCrlfCrlf(buf) {
    for (let i = 0; i + 3 < buf.length; i++) {
      if (buf[i] === 13 && buf[i + 1] === 10 && buf[i + 2] === 13 && buf[i + 3] === 10) {
        return i;
      }
    }
    return -1;
  }

  // Parses "HTTP/1.1 200 OK\r\nName: value\r\n..." (without the blank line).
  function parseHead(text) {
    const lines = text.split('\r\n');
    const m = /^HTTP\/\d\.\d (\d{3})\s?(.*)$/.exec(lines[0]);
    if (!m) {
      return null;
    }
    const headers = [];
    for (const line of lines.slice(1)) {
      const colon = line.indexOf(':');
      if (colon > 0) {
        headers.push([line.slice(0, colon).trim(), line.slice(colon + 1).trim()]);
      }
    }
    return { status: Number(m[1]), statusText: m[2], headers };
  }

  function getHeader(headers, name) {
    name = name.toLowerCase();
    const h = headers.find(([k]) => k.toLowerCase() === name);
    return h ? h[1] : null;
  }

  function randomBytes(n) {
    const b = new Uint8Array(n);
    crypto.getRandomValues(b);
    return b;
  }

  function base64(bytes) {
    return btoa(latin1(bytes));
  }

  // Headers from the browser's request that must not be forwarded as-is:
  // hop-by-hop ones, ones we set ourselves, and cookies (the jar owns those).
  const SKIP_REQUEST_HEADERS = new Set([
    'host', 'connection', 'keep-alive', 'transfer-encoding', 'content-length', 'upgrade',
    'cookie', 'accept-encoding', 'te', 'trailer', 'proxy-connection',
  ]);
  // Headers from FluidNC's response that describe the wire, not the content.
  const SKIP_RESPONSE_HEADERS = new Set(['transfer-encoding', 'connection', 'keep-alive', 'set-cookie']);

  function createClient(Module) {
    const vOpen = Module.cwrap('fluidnc_vconn_open', 'number', ['number']);
    const vClose = Module.cwrap('fluidnc_vconn_close', null, ['number']);
    const conns = new Map();  // id -> { data(Uint8Array), closed() }
    const cookies = new Map();

    global.fluidncOnVconnData = (id, bytes) => {
      const c = conns.get(id);
      if (c) {
        c.data(bytes);
      }
    };
    global.fluidncOnVconnClose = (id) => {
      const c = conns.get(id);
      if (c) {
        conns.delete(id);
        c.closed();
      }
    };

    function send(id, bytes) {
      for (let off = 0; off < bytes.length; off += SEND_CHUNK) {
        const part = bytes.subarray(off, off + SEND_CHUNK);
        Module.ccall('fluidnc_vconn_send', null, ['number', 'array', 'number'], [id, part, part.length]);
      }
    }

    // Opens a connection, waiting for the server to come up if FluidNC is
    // still starting.  Handlers are registered in the same tick as the open,
    // before any output for the id can be delivered (output is proxied to
    // this thread and so waits until we yield).
    async function connect(handlers, timeoutMs = 15000) {
      const start = Date.now();
      for (;;) {
        const id = vOpen(HTTP_PORT);
        if (id) {
          conns.set(id, handlers);
          return id;
        }
        if (Date.now() - start > timeoutMs) {
          return 0;
        }
        await new Promise((r) => setTimeout(r, 100));
      }
    }

    function cookieHeader() {
      return [...cookies].map(([k, v]) => k + '=' + v).join('; ');
    }
    function storeCookies(headers) {
      for (const [k, v] of headers) {
        if (k.toLowerCase() === 'set-cookie') {
          const pair = v.split(';')[0];
          const eq = pair.indexOf('=');
          if (eq > 0) {
            cookies.set(pair.slice(0, eq).trim(), pair.slice(eq + 1).trim());
          }
        }
      }
    }

    function requestHead(method, path, headers, extra) {
      let s = method + ' ' + path + ' HTTP/1.1\r\nHost: fluidnc\r\n';
      for (const [k, v] of headers) {
        if (!SKIP_REQUEST_HEADERS.has(k.toLowerCase())) {
          s += k + ': ' + v + '\r\n';
        }
      }
      const ck = cookieHeader();
      if (ck) {
        s += 'Cookie: ' + ck + '\r\n';
      }
      for (const line of extra) {
        s += line + '\r\n';
      }
      return utf8.encode(s + '\r\n');
    }

    // ---- HTTP ----
    //
    // sink: { head(status, statusText, headers), data(Uint8Array), end(), error(message) }
    // Returns a cancel function.
    function http({ method, path, headers = [], body = null }, sink) {
      let id = 0;
      let cancelled = false;
      let buf = new Uint8Array(0);
      let state = 'head';  // head -> body (length | chunked | close) -> done
      let remaining = 0;   // bytes left in a Content-Length body or current chunk
      let mode = 'close';
      let chunkPhase = 'size';  // size -> data -> crlf -> (size | trailer)

      function finish() {
        if (state !== 'done') {
          state = 'done';
          sink.end();
          if (id && conns.has(id)) {
            conns.delete(id);
            vClose(id);
          }
        }
      }

      function emit(bytes) {
        if (bytes.length) {
          sink.data(bytes.slice());
        }
      }

      function onData(bytes) {
        if (state === 'done') {
          return;
        }
        buf = concat(buf, bytes);
        if (state === 'head') {
          const end = indexOfCrlfCrlf(buf);
          if (end < 0) {
            return;
          }
          const head = parseHead(latin1(buf.subarray(0, end)));
          buf = buf.slice(end + 4);
          if (!head) {
            state = 'done';
            sink.error('Malformed response from FluidNC');
            return;
          }
          storeCookies(head.headers);
          const te = getHeader(head.headers, 'transfer-encoding');
          const cl = getHeader(head.headers, 'content-length');
          if (te && /chunked/i.test(te)) {
            mode = 'chunked';
          } else if (cl !== null) {
            mode = 'length';
            remaining = Number(cl);
          }
          sink.head(head.status, head.statusText, head.headers.filter(([k]) => !SKIP_RESPONSE_HEADERS.has(k.toLowerCase())));
          state = 'body';
          if (method === 'HEAD' || head.status === 204 || head.status === 304 || (mode === 'length' && remaining === 0)) {
            finish();
            return;
          }
        }
        if (mode === 'close') {
          emit(buf);
          buf = new Uint8Array(0);
        } else if (mode === 'length') {
          const n = Math.min(remaining, buf.length);
          emit(buf.subarray(0, n));
          buf = buf.slice(n);
          remaining -= n;
          if (remaining === 0) {
            finish();
          }
        } else {
          decodeChunks();
        }
      }

      function decodeChunks() {
        for (;;) {
          if (chunkPhase === 'size' || chunkPhase === 'trailer') {
            let eol = -1;
            for (let i = 0; i + 1 < buf.length; i++) {
              if (buf[i] === 13 && buf[i + 1] === 10) {
                eol = i;
                break;
              }
            }
            if (eol < 0) {
              return;
            }
            const line = latin1(buf.subarray(0, eol));
            buf = buf.slice(eol + 2);
            if (chunkPhase === 'trailer') {
              if (line === '') {
                finish();
                return;
              }
              continue;  // ignore trailer headers
            }
            remaining = parseInt(line, 16);
            if (Number.isNaN(remaining)) {
              state = 'done';
              sink.error('Bad chunk size from FluidNC');
              return;
            }
            chunkPhase = remaining === 0 ? 'trailer' : 'data';
          } else if (chunkPhase === 'data') {
            if (!buf.length) {
              return;
            }
            const n = Math.min(remaining, buf.length);
            emit(buf.subarray(0, n));
            buf = buf.slice(n);
            remaining -= n;
            if (remaining === 0) {
              chunkPhase = 'crlf';
            }
          } else {  // crlf after chunk data
            if (buf.length < 2) {
              return;
            }
            buf = buf.slice(2);
            chunkPhase = 'size';
          }
        }
      }

      // Only a close-delimited body ends at EOF.  A Content-Length or
      // chunked body that is cut short is an error, not a short success.
      function onClosed() {
        if (state === 'done') {
          return;
        }
        if (state === 'body' && mode === 'close') {
          finish();
          return;
        }
        const message = state === 'head'
          ? 'FluidNC closed the connection without a response'
          : 'FluidNC closed the connection before the response was complete';
        state = 'done';
        sink.error(message);
      }

      connect({ data: onData, closed: onClosed }).then((cid) => {
        if (!cid) {
          sink.error('FluidNC web server is not running');
          return;
        }
        id = cid;
        if (cancelled) {
          conns.delete(id);
          vClose(id);
          return;
        }
        const bodyBytes = body ? new Uint8Array(body) : null;
        const extra = ['Accept-Encoding: gzip', 'Connection: close'];
        if (bodyBytes) {
          extra.push('Content-Length: ' + bodyBytes.length);
        }
        send(id, requestHead(method, path, headers, extra));
        if (bodyBytes && bodyBytes.length) {
          send(id, bodyBytes);
        }
      });

      return function cancel() {
        cancelled = true;
        if (id && conns.has(id)) {
          conns.delete(id);
          vClose(id);
        }
        state = 'done';
      };
    }

    // ---- WebSocket ----
    //
    // sink: { open(protocol), text(string), binary(ArrayBuffer), close(code, reason, clean), error() }
    // Returns { sendText(s), sendBinary(ArrayBuffer), close(code, reason) }.
    function websocket(path, protocols, sink) {
      let id = 0;
      let state = 'connecting';  // connecting -> open -> closing -> closed
      let buf = new Uint8Array(0);
      let fragments = null;       // { opcode, parts[] } while reassembling
      let closeInfo = null;       // { code, reason } from the server's close frame
      let sentClose = false;
      const pendingOut = [];       // frames queued before the handshake finished

      function frame(opcode, payload) {
        const len = payload.length;
        const hdrLen = len < 126 ? 2 : len < 65536 ? 4 : 10;
        const out = new Uint8Array(hdrLen + 4 + len);
        out[0] = 0x80 | opcode;
        if (len < 126) {
          out[1] = 0x80 | len;
        } else if (len < 65536) {
          out[1] = 0x80 | 126;
          out[2] = len >> 8;
          out[3] = len & 0xff;
        } else {
          out[1] = 0x80 | 127;
          new DataView(out.buffer).setBigUint64(2, BigInt(len));
        }
        const mask = randomBytes(4);  // client frames must be masked
        out.set(mask, hdrLen);
        for (let i = 0; i < len; i++) {
          out[hdrLen + 4 + i] = payload[i] ^ mask[i & 3];
        }
        return out;
      }

      function write(opcode, payload) {
        const f = frame(opcode, payload);
        if (state === 'open' || (state === 'closing' && opcode === 8)) {
          send(id, f);
        } else if (state === 'connecting') {
          pendingOut.push(f);
        }
      }

      function shutdown(clean) {
        if (state === 'closed') {
          return;
        }
        state = 'closed';
        if (id && conns.has(id)) {
          conns.delete(id);
          vClose(id);
        }
        const code = closeInfo ? closeInfo.code : 1006;
        sink.close(code, closeInfo ? closeInfo.reason : '', clean && !!closeInfo);
      }

      function handleFrame(fin, opcode, payload) {
        if (opcode === 0x8) {  // close
          const code = payload.length >= 2 ? (payload[0] << 8) | payload[1] : 1005;
          closeInfo = { code, reason: utf8dec.decode(payload.subarray(2)) };
          if (!sentClose) {
            sentClose = true;
            state = 'closing';
            write(0x8, payload.subarray(0, 2));
          }
          shutdown(true);
          return;
        }
        if (opcode === 0x9) {  // ping
          write(0xA, payload);
          return;
        }
        if (opcode === 0xA) {  // pong
          return;
        }
        if (opcode === 0x0) {  // continuation
          if (!fragments) {
            return;
          }
          fragments.parts.push(payload);
        } else {
          fragments = { opcode, parts: [payload] };
        }
        if (!fin) {
          return;
        }
        let whole = fragments.parts.length === 1 ? fragments.parts[0] : fragments.parts.reduce(concat, new Uint8Array(0));
        const op = fragments.opcode;
        fragments = null;
        if (op === 0x1) {
          sink.text(utf8dec.decode(whole));
        } else if (op === 0x2) {
          whole = whole.slice();
          sink.binary(whole.buffer);
        }
      }

      function parseFrames() {
        for (;;) {
          if (buf.length < 2) {
            return;
          }
          const fin = (buf[0] & 0x80) !== 0;
          const opcode = buf[0] & 0x0f;
          const masked = (buf[1] & 0x80) !== 0;
          let len = buf[1] & 0x7f;
          let off = 2;
          if (len === 126) {
            if (buf.length < 4) {
              return;
            }
            len = (buf[2] << 8) | buf[3];
            off = 4;
          } else if (len === 127) {
            if (buf.length < 10) {
              return;
            }
            len = Number(new DataView(buf.buffer, buf.byteOffset).getBigUint64(2));
            off = 10;
          }
          const maskOff = off;
          if (masked) {
            off += 4;
          }
          if (buf.length < off + len) {
            return;
          }
          let payload = buf.slice(off, off + len);
          if (masked) {
            for (let i = 0; i < len; i++) {
              payload[i] ^= buf[maskOff + (i & 3)];
            }
          }
          buf = buf.slice(off + len);
          handleFrame(fin, opcode, payload);
          if (state === 'closed') {
            return;
          }
        }
      }

      const key = base64(randomBytes(16));
      function onData(bytes) {
        buf = concat(buf, bytes);
        if (state === 'connecting') {
          const end = indexOfCrlfCrlf(buf);
          if (end < 0) {
            return;
          }
          const head = parseHead(latin1(buf.subarray(0, end)));
          buf = buf.slice(end + 4);
          if (!head || head.status !== 101) {
            sink.error();
            shutdown(false);
            return;
          }
          storeCookies(head.headers);
          state = 'open';
          sink.open(getHeader(head.headers, 'sec-websocket-protocol') || '');
          for (const f of pendingOut.splice(0)) {
            send(id, f);
          }
        }
        if (state === 'open' || state === 'closing') {
          parseFrames();
        }
      }

      connect({ data: onData, closed: () => shutdown(sentClose) }).then((cid) => {
        if (state === 'closed') {
          // Closed by the caller while connect() was still waiting for the
          // server: don't open an upgrade nobody will ever use.
          if (cid) {
            conns.delete(cid);
            vClose(cid);
          }
          return;
        }
        if (!cid) {
          sink.error();
          shutdown(false);
          return;
        }
        id = cid;
        const extra = [
          'Upgrade: websocket',
          'Connection: Upgrade',
          'Sec-WebSocket-Key: ' + key,
          'Sec-WebSocket-Version: 13',
        ];
        if (protocols.length) {
          extra.push('Sec-WebSocket-Protocol: ' + protocols.join(', '));
        }
        send(id, requestHead('GET', path, [], extra));
      });

      return {
        sendText(s) {
          write(0x1, utf8.encode(s));
        },
        sendBinary(ab) {
          write(0x2, new Uint8Array(ab));
        },
        close(code = 1000, reason = '') {
          if (state === 'closed' || sentClose) {
            return;
          }
          if (state === 'connecting') {
            shutdown(false);
            return;
          }
          sentClose = true;
          const r = utf8.encode(reason);
          const p = new Uint8Array(2 + r.length);
          p[0] = code >> 8;
          p[1] = code & 0xff;
          p.set(r, 2);
          write(0x8, p);
          state = 'closing';
        },
      };
    }

    return { http, websocket, cookies };
  }

  // ---- Browser wiring ----

  function waitActive(reg) {
    const sw = reg.active || reg.waiting || reg.installing;
    if (!sw || sw.state === 'activated') {
      return Promise.resolve(reg.active);
    }
    return new Promise((resolve) => {
      sw.addEventListener('statechange', () => {
        if (sw.state === 'activated') {
          resolve(sw);
        }
      });
    });
  }

  let deviceUrlPromise = null;
  let attachedClient = null;
  let attachedToken = null;
  let deviceHrefResolved = null;  // absolute device URL, once the worker is active

  // Wires the client to the Service Worker and to shim WebSockets in the
  // device iframe.  Returns false if this wasm build has no web server.
  function attach(Module) {
    if (typeof Module._fluidnc_vconn_open !== 'function') {
      return false;
    }
    const client = createClient(Module);
    attachedClient = client;
    const token = base64(randomBytes(12)).replace(/[^A-Za-z0-9]/g, '');
    attachedToken = token;

    // HTTP requests relayed by vnet-sw.js.  onmessage (not addEventListener)
    // so that message delivery from the worker starts immediately.
    navigator.serviceWorker.onmessage = (event) => {
      const m = event.data;
      if (m && m.type === 'vnet-who-has' && event.ports[0]) {
        // vnet-sw.js recovering, after a restart, which page owns a frame.
        event.ports[0].postMessage({ mine: m.token === token });
        return;
      }
      if (!m || m.type !== 'vnet-http') {
        return;
      }
      const port = event.ports[0];
      const cancel = client.http(m, {
        head: (status, statusText, headers) => port.postMessage({ type: 'head', status, statusText, headers }),
        data: (bytes) => port.postMessage({ type: 'data', data: bytes.buffer }, [bytes.buffer]),
        end: () => {
          port.postMessage({ type: 'end' });
          port.close();
        },
        error: (message) => {
          port.postMessage({ type: 'error', message });
          port.close();
        },
      });
      port.onmessage = (e) => {
        if (e.data && e.data.type === 'cancel') {
          cancel();
        }
      };
    };

    // WebSockets opened by vnet-ws-shim.js inside the iframe.
    window.addEventListener('message', (event) => {
      const m = event.data;
      if (!m || m.type !== 'vnet-ws-open' || event.origin !== location.origin || !event.ports[0]) {
        return;
      }
      const port = event.ports[0];
      const ws = client.websocket(m.path, m.protocols || [], {
        open: (protocol) => port.postMessage({ type: 'open', protocol }),
        text: (data) => port.postMessage({ type: 'text', data }),
        binary: (ab) => port.postMessage({ type: 'binary', data: ab }, [ab]),
        error: () => port.postMessage({ type: 'error' }),
        close: (code, reason, clean) => {
          port.postMessage({ type: 'close', code, reason, clean });
          port.close();
        },
      });
      port.onmessage = (e) => {
        const d = e.data;
        if (d.type === 'text') {
          ws.sendText(d.data);
        } else if (d.type === 'binary') {
          ws.sendBinary(d.data);
        } else if (d.type === 'close') {
          ws.close(d.code, d.reason);
        }
      };
    });

    deviceUrlPromise = navigator.serviceWorker
      .register('vnet-sw.js', { scope: 'device/' })
      .then(waitActive)
      .then((sw) => {
        sw.postMessage({ type: 'vnet-host', token });
        deviceHrefResolved = new URL('device/?vnethost=' + token, location.href).href;
        return 'device/?vnethost=' + token;
      });
    return true;
  }

  // URL to load in the iframe once the Service Worker is active.
  function deviceUrl() {
    return deviceUrlPromise || Promise.reject(new Error('virtual network not attached'));
  }

  // The same, synchronously: null until the worker is active.  Used by the
  // demo page's frame guard (see the top of index.html), which must decide
  // before any other script runs.
  function deviceHref() {
    return deviceHrefResolved;
  }

  // An HTTP request from this page straight to FluidNC's web server, the
  // way a browser on the LAN would make it (used by the demo's WebUI
  // installer).  Resolves to { status, headers, body: Uint8Array }.
  function fetchDevice(method, path, { headers = [], body = null } = {}) {
    if (!attachedClient) {
      return Promise.reject(new Error('virtual network not attached'));
    }
    return new Promise((resolve, reject) => {
      const res = { status: 0, statusText: '', headers: [], parts: [] };
      attachedClient.http({ method, path, headers, body }, {
        head: (status, statusText, h) => Object.assign(res, { status, statusText, headers: h }),
        data: (bytes) => res.parts.push(bytes),
        end: () => {
          const total = res.parts.reduce((n, p) => n + p.length, 0);
          const out = new Uint8Array(total);
          let off = 0;
          for (const p of res.parts) {
            out.set(p, off);
            off += p.length;
          }
          resolve({ status: res.status, statusText: res.statusText, headers: res.headers, body: out });
        },
        error: (message) => reject(new Error(message)),
      });
    });
  }

  // ---- Names that reach the device ----
  //
  // Like a real network: the device answers at <$Hostname>.local (mDNS) and
  // at its IP address, both as FluidNC itself reports them in [ESP800], so
  // changing $Hostname changes which name works.  Defaults until the first
  // refresh match VirtualNet.cpp.
  let identity = { hostName: 'fluidnc', ip: '192.168.0.1' };

  async function refreshIdentity() {
    try {
      const r = await fetchDevice('GET', '/command?plain=' + encodeURIComponent('[ESP800]json=yes'));
      const d = JSON.parse(utf8dec.decode(r.body)).data;
      identity = { hostName: d.HostName, ip: d.WebSocketIP };
    } catch (e) {
      // keep the last known identity
    }
    return identity;
  }

  function deviceNames() {
    return [identity.hostName.toLowerCase() + '.local', identity.ip];
  }

  function isDeviceHost(host) {
    return deviceNames().includes(String(host).toLowerCase());
  }

  // Path of the Service Worker's scope, e.g. "/device/".
  function scopePath() {
    return new URL('device/', location.href).pathname;
  }

  // Iframe URL for a device path like "/files?path=/#x".
  function deviceHrefFor(pathQueryHash) {
    // The query is kept as typed (not re-encoded), plus the host token.
    const want = new URL(pathQueryHash, 'http://device');
    const u = new URL(deviceHrefResolved || new URL('device/', location.href).href);
    const token = u.searchParams.get('vnethost');
    u.pathname = scopePath() + want.pathname.replace(/^\//, '');
    u.search = token ? want.search + (want.search ? '&' : '?') + 'vnethost=' + token : want.search;
    u.hash = want.hash;
    return u.href;
  }

  // The device path ("/x?y#z") an iframe location stands for, or null if
  // the location is not under the device scope.
  function devicePathOf(loc) {
    const u = new URL(loc);
    if (u.origin !== location.origin || !u.pathname.startsWith(scopePath())) {
      return null;
    }
    const search = u.search.replace(/([?&])vnethost=[^&]*(&|$)/, (m, lead, tail) => (tail ? lead : '')).replace(/^\?$/, '');
    return '/' + u.pathname.slice(scopePath().length) + search + u.hash;
  }

  // This page's token, which ties its device frame to it (see vnet-sw.js).
  function token() {
    return attachedToken;
  }

  const api = {
    attach, deviceUrl, deviceHref, fetchDevice, createClient, token,
    refreshIdentity, deviceNames, isDeviceHost, scopePath, deviceHrefFor, devicePathOf,
  };
  global.FluidNCVnet = api;
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = api;
  }
})(typeof self !== 'undefined' ? self : globalThis);
