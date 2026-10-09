// Service Worker for the wasm demo's "device" iframe: makes the FluidNC web
// server running inside the wasm build look like a real HTTP server.
//
// It is registered by vnet-host.js with scope "device/".  The WebUI loaded in
// the iframe at device/ is therefore controlled by this worker, and so is
// every same-origin request that WebUI makes -- including absolute ones like
// /command, /files, /upload and /sd/..., which lie outside the scope: scope
// decides which *pages* are controlled, not which URLs are intercepted.
//
// For each such request this worker posts {method, path, headers, body} to
// the host page (the demo page that owns the wasm instance), together with a
// MessagePort on which the host streams back {head}, {data}..., {end}.  The
// host does all the HTTP work over the fluidnc_vconn_* exports; see
// vnet-host.js.  This worker only adapts to and from the Fetch API, plus:
//
// - gzip: FluidNC serves index.html.gz with Content-Encoding: gzip, and a
//   browser does not decode a Response the worker constructs itself, so it is
//   decoded here.
// - Cross-origin isolation: the demo page is COOP/COEP-isolated (needed for
//   SharedArrayBuffer), so a nested document must also send COEP.
// - WebSockets are invisible to Service Workers, so HTML documents get
//   vnet-ws-shim.js inlined at the top of <head>, replacing window.WebSocket.
//
// Requests to other origins (CDNs etc.) are not touched.

const SCOPE_PATH = new URL(self.registration.scope).pathname;  // e.g. "/device/"

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

// Each demo page (the "host") owns one FluidNC instance and announces
// itself with a random token; its iframe is loaded as
// device/?vnethost=<token>.  Every request must reach the host that owns
// the requesting frame -- with several demo tabs open, a request sent to
// the wrong one would run commands and write files on another tab's
// FluidNC.
//
// The maps below are only a cache: the browser may stop this worker when
// idle, losing them.  findHost() then recovers by asking (frame -> token,
// token -> host) and, if that fails, guesses only when exactly one demo
// page is open; otherwise the request fails rather than risk the wrong one.
const hostByToken = new Map();     // token -> host clientId
const tokenByClient = new Map();   // device frame clientId -> token

self.addEventListener('message', (event) => {
  const m = event.data;
  if (m && m.type === 'vnet-host' && event.source) {
    hostByToken.set(m.token, event.source.id);
  }
});

// Posts msg to a client with a reply port; resolves to the reply, or null
// if none arrives in time.
function ask(client, msg, timeoutMs = 1000) {
  return new Promise((resolve) => {
    const { port1, port2 } = new MessageChannel();
    const timer = setTimeout(() => {
      port1.close();
      resolve(null);
    }, timeoutMs);
    port1.onmessage = (e) => {
      clearTimeout(timer);
      port1.close();
      resolve(e.data);
    };
    client.postMessage(msg, [port2]);
  });
}

async function hostPages() {
  const all = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
  return all.filter((c) => !new URL(c.url).pathname.startsWith(SCOPE_PATH));
}

async function findHost(event, urlToken) {
  // 1. Which host does the requesting frame belong to?  A frame's own
  // navigation carries the token; later requests from it are matched by
  // its clientId, or, after a restart, by asking the frame itself (the
  // injected vnet-ws-shim.js answers with its parent's token).
  let token = urlToken || tokenByClient.get(event.clientId);
  if (!token && event.clientId) {
    const frame = await self.clients.get(event.clientId);
    const reply = frame && (await ask(frame, { type: 'vnet-which-host' }));
    token = reply && reply.token;
  }
  if (token) {
    if (event.clientId) {
      tokenByClient.set(event.clientId, token);
    }
    if (event.resultingClientId) {
      tokenByClient.set(event.resultingClientId, token);
    }

    // 2. Which page owns that token?  Ask the pages after a restart.
    const known = hostByToken.get(token);
    const host = known && (await self.clients.get(known));
    if (host) {
      return host;
    }
    for (const page of await hostPages()) {
      const reply = await ask(page, { type: 'vnet-who-has', token });
      if (reply && reply.mine) {
        hostByToken.set(token, page.id);
        return page;
      }
    }
  }

  // 3. Unattributable: only safe when there is a single demo page.
  const pages = await hostPages();
  return pages.length === 1 ? pages[0] : null;
}

// Fetched per document load rather than cached here, so an edited shim
// takes effect without waiting for this worker to be replaced.
async function getShim() {
  const res = await fetch(new URL('vnet-ws-shim.js', self.registration.scope.replace(/device\/$/, '')), { cache: 'no-cache' });
  return res.ok ? res.text() : '';
}

// Inserts <script>shim</script> just after <head ...> (or the doctype, or at
// the very start), so it runs before any of the page's own scripts.
function injectScript(source) {
  const tag = '<script>' + source.replace(/<\/script/gi, '<\\/script') + '</script>';
  let pending = '';
  let done = false;
  function insert(text) {
    let at = 0;
    let m = /<head(\s[^>]*)?>/i.exec(text);
    if (m) {
      at = m.index + m[0].length;
    } else if ((m = /^\s*<!doctype[^>]*>/i.exec(text))) {
      at = m[0].length;
    }
    return text.slice(0, at) + tag + text.slice(at);
  }
  return new TransformStream({
    transform(chunk, controller) {
      if (done) {
        controller.enqueue(chunk);
        return;
      }
      pending += chunk;
      if (!/<head(\s[^>]*)?>/i.test(pending) && pending.length < 4096) {
        return;  // wait for more before deciding
      }
      controller.enqueue(insert(pending));
      pending = '';
      done = true;
    },
    flush(controller) {
      if (!done) {
        controller.enqueue(insert(pending));
      }
    },
  });
}

self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin) {
    return;  // let the browser handle other origins
  }
  event.respondWith(forward(event, url));
});

async function forward(event, url) {
  const req = event.request;

  // device/... maps to the server root; absolute paths pass through as-is.
  // The token is removed from the raw query, which is otherwise forwarded
  // exactly as the page wrote it.
  const token = url.searchParams.get('vnethost');
  const search = url.search.replace(/([?&])vnethost=[^&]*(&|$)/, (m, lead, tail) => (tail ? lead : '')).replace(/^\?$/, '');
  let path = url.pathname;
  if (path.startsWith(SCOPE_PATH)) {
    path = '/' + path.slice(SCOPE_PATH.length);
  }
  path += search;

  const host = await findHost(event, token);
  if (!host) {
    return new Response('Cannot tell which demo page this frame belongs to -- reload the demo page', { status: 503 });
  }

  const headers = [];
  for (const [k, v] of req.headers) {
    headers.push([k, v]);
  }
  let body = null;
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    body = await req.arrayBuffer();
  }

  const { port1, port2 } = new MessageChannel();
  const msg = { type: 'vnet-http', method: req.method, path, headers, body };
  host.postMessage(msg, body ? [port2, body] : [port2]);

  const shim = req.mode === 'navigate' ? await getShim() : '';

  return new Promise((resolve) => {
    let controller = null;
    let resolved = false;
    const stream = new ReadableStream({
      start(c) {
        controller = c;
      },
      cancel() {
        port1.postMessage({ type: 'cancel' });
        port1.close();
      },
    });

    port1.onmessage = (e) => {
      const m = e.data;
      switch (m.type) {
        case 'head': {
          const h = new Headers();
          for (const [k, v] of m.headers) {
            h.append(k, v);
          }
          let bodyStream = stream;
          if (/gzip/i.test(h.get('content-encoding') || '')) {
            bodyStream = bodyStream.pipeThrough(new DecompressionStream('gzip'));
            h.delete('content-encoding');
            h.delete('content-length');
          }
          if (req.mode === 'navigate' && shim && /text\/html/i.test(h.get('content-type') || '')) {
            bodyStream = bodyStream
              .pipeThrough(new TextDecoderStream())
              .pipeThrough(injectScript(shim))
              .pipeThrough(new TextEncoderStream());
            h.delete('content-length');
          }
          // A redirect to "/" inside the iframe must stay inside the scope.
          const loc = h.get('location');
          if (req.mode === 'navigate' && loc && loc.startsWith('/') && !loc.startsWith(SCOPE_PATH)) {
            h.set('location', SCOPE_PATH + loc.slice(1));
          }
          h.set('Cross-Origin-Embedder-Policy', 'require-corp');
          h.set('Cross-Origin-Resource-Policy', 'same-origin');
          const noBody = req.method === 'HEAD' || [204, 205, 304].includes(m.status);
          resolved = true;
          resolve(new Response(noBody ? null : bodyStream, { status: m.status, statusText: m.statusText, headers: h }));
          break;
        }
        case 'data':
          controller.enqueue(new Uint8Array(m.data));
          break;
        case 'end':
          controller.close();
          port1.close();
          break;
        case 'error':
          if (resolved) {
            controller.error(new Error(m.message));
          } else {
            resolve(new Response(m.message, { status: 502, statusText: 'Bad Gateway' }));
          }
          port1.close();
          break;
      }
    };
  });
}
