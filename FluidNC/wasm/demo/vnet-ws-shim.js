// WebSocket stand-in for a WebUI running in the wasm demo's device iframe.
//
// vnet-sw.js inlines this at the top of every HTML document it serves.  A
// Service Worker cannot intercept WebSocket connections, so this replaces
// window.WebSocket with an object that behaves the same but carries the
// connection over a MessagePort to the host page (vnet-host.js), which runs
// the real WebSocket protocol against FluidNC's own server inside the wasm
// instance.
//
// Only URLs that point at "the device" are redirected: the page's own host,
// or a name the device answers to (its IP and <$Hostname>.local, as the
// host page learns them from [ESP800]) -- WebUI2, for example, builds
// ws://<WebSocketIP>:<WebSocketPort> from [ESP800].  Anything else still
// gets a real WebSocket.
//
// It also makes the iframe behave like a browser pointed at the device:
// same-device links stay in the demo's browser pane (including
// target=_blank ones, and absolute "/..." links, which would otherwise
// leave the Service Worker's scope), and URL changes that do not reload the
// page (history.pushState, #hash) are reported so the demo's URL bar can
// follow them.

(function () {
  if (window.__fluidncVnetShim) {
    return;
  }
  window.__fluidncVnetShim = true;

  const RealWebSocket = window.WebSocket;
  const host = window.parent !== window ? window.parent : null;
  if (!host) {
    return;  // not inside the demo page; nothing to relay through
  }

  const vnet = host.FluidNCVnet;  // same origin, so directly reachable

  // The page's own hostname counts on any port: stock WebUIs build their
  // WebSocket URL from it with a port of their own choosing -- WebUI 3
  // v3.0.x uses the page's port + 2 (a FluidNC 3.x leftover), FigUI uses
  // the port from [ESP800] -- and on a controller that is the device.
  function isDevice(u) {
    if (u.hostname === location.hostname) {
      return true;
    }
    try {
      return !!(vnet && vnet.isDeviceHost(u.hostname));
    } catch (e) {
      return false;
    }
  }

  // Where a same-device URL should load in this frame: under the Service
  // Worker's scope, with the path the device would see.
  function inScope(u) {
    const scope = vnet ? vnet.scopePath() : '/device/';
    if (u.host === location.host && u.pathname.startsWith(scope)) {
      return u.href;
    }
    const target = new URL(location.href);
    target.pathname = scope + u.pathname.replace(/^\//, '');
    target.search = u.search;
    target.hash = u.hash;
    return target.href;
  }

  // Links: run last (bubbling, at window) so the page's own handlers --
  // e.g. a single-page app's router -- get first say.
  window.addEventListener('click', (e) => {
    if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) {
      return;
    }
    const a = e.target.closest && e.target.closest('a[href]');
    if (!a || a.hasAttribute('download')) {
      return;
    }
    const u = new URL(a.href, location.href);
    if (!/^https?:$/.test(u.protocol) || !isDevice(u)) {
      return;
    }
    const target = (a.target || '').toLowerCase();
    const sameFrame = target === '' || target === '_self';
    const inScopeAlready = u.host === location.host && u.pathname.startsWith(vnet ? vnet.scopePath() : '/device/');
    if (sameFrame && inScopeAlready) {
      return;  // an ordinary in-scope link; let the browser follow it
    }
    e.preventDefault();
    location.href = inScope(u);
  });

  const realOpen = window.open;
  window.open = function (url, target, features) {
    if (url) {
      const u = new URL(url, location.href);
      if (/^https?:$/.test(u.protocol) && isDevice(u)) {
        location.href = inScope(u);
        return window;
      }
    }
    return realOpen.apply(this, arguments);
  };

  // vnet-sw.js, after the browser has restarted it, asks which demo page
  // this frame belongs to.  Messages from the worker are held until
  // startMessages() (or an onmessage handler) enables them.
  if (navigator.serviceWorker) {
    navigator.serviceWorker.addEventListener('message', (e) => {
      if (e.data && e.data.type === 'vnet-which-host' && e.ports[0]) {
        e.ports[0].postMessage({ token: vnet ? vnet.token() : null });
      }
    });
    navigator.serviceWorker.startMessages();
  }

  function reportLocation() {
    try {
      host.postMessage({ type: 'vnet-location' }, location.origin);
    } catch (e) {}
  }
  for (const name of ['pushState', 'replaceState']) {
    const real = history[name];
    history[name] = function () {
      const r = real.apply(this, arguments);
      reportLocation();
      return r;
    };
  }
  window.addEventListener('popstate', reportLocation);
  window.addEventListener('hashchange', reportLocation);

  class VnetWebSocket extends EventTarget {
    constructor(url, protocols) {
      super();
      const u = new URL(url, location.href);
      if (!isDevice(u)) {
        return new RealWebSocket(url, protocols);
      }
      this.url = u.href;
      this.readyState = VnetWebSocket.CONNECTING;
      this.protocol = '';
      this.extensions = '';
      this.bufferedAmount = 0;
      this._binaryType = 'blob';
      this.onopen = this.onmessage = this.onclose = this.onerror = null;

      const ch = new MessageChannel();
      this._port = ch.port1;
      this._port.onmessage = (e) => this._fromHost(e.data);
      const list = protocols == null ? [] : [].concat(protocols);
      host.postMessage({ type: 'vnet-ws-open', path: u.pathname + u.search, protocols: list }, location.origin, [ch.port2]);
    }

    get binaryType() {
      return this._binaryType;
    }
    set binaryType(v) {
      if (v === 'blob' || v === 'arraybuffer') {
        this._binaryType = v;
      }
    }

    _fire(type, init) {
      let ev;
      if (type === 'message') {
        ev = new MessageEvent('message', init);
      } else if (type === 'close') {
        ev = new CloseEvent('close', init);
      } else {
        ev = new Event(type);
      }
      this.dispatchEvent(ev);
      const handler = this['on' + type];
      if (typeof handler === 'function') {
        handler.call(this, ev);
      }
    }

    _fromHost(m) {
      const origin = new URL(this.url).origin;
      switch (m.type) {
        case 'open':
          this.readyState = VnetWebSocket.OPEN;
          this.protocol = m.protocol || '';
          this._fire('open');
          break;
        case 'text':
          this._fire('message', { data: m.data, origin });
          break;
        case 'binary':
          // m.data arrived by structured clone, so it is an ArrayBuffer of
          // this realm and `e.data instanceof ArrayBuffer` checks work.
          this._fire('message', { data: this._binaryType === 'arraybuffer' ? m.data : new Blob([m.data]), origin });
          break;
        case 'error':
          this._fire('error');
          break;
        case 'close':
          if (this.readyState === VnetWebSocket.CLOSED) {
            return;
          }
          this.readyState = VnetWebSocket.CLOSED;
          this._fire('close', { code: m.code || 1006, reason: m.reason || '', wasClean: !!m.clean });
          this._port.close();
          break;
      }
    }

    send(data) {
      if (this.readyState === VnetWebSocket.CONNECTING) {
        throw new DOMException("Failed to execute 'send' on 'WebSocket': Still in CONNECTING state.", 'InvalidStateError');
      }
      if (this.readyState !== VnetWebSocket.OPEN) {
        return;
      }
      if (typeof data === 'string') {
        this._port.postMessage({ type: 'text', data });
      } else if (data instanceof Blob) {
        data.arrayBuffer().then((b) => this._port.postMessage({ type: 'binary', data: b }, [b]));
      } else if (ArrayBuffer.isView(data)) {
        const b = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
        this._port.postMessage({ type: 'binary', data: b }, [b]);
      } else if (data instanceof ArrayBuffer) {
        const b = data.slice(0);
        this._port.postMessage({ type: 'binary', data: b }, [b]);
      } else {
        this._port.postMessage({ type: 'text', data: String(data) });
      }
    }

    close(code, reason) {
      if (this.readyState === VnetWebSocket.CLOSING || this.readyState === VnetWebSocket.CLOSED) {
        return;
      }
      this.readyState = VnetWebSocket.CLOSING;
      this._port.postMessage({ type: 'close', code, reason });
    }
  }

  for (const [name, value] of [['CONNECTING', 0], ['OPEN', 1], ['CLOSING', 2], ['CLOSED', 3]]) {
    Object.defineProperty(VnetWebSocket, name, { value });
    Object.defineProperty(VnetWebSocket.prototype, name, { value });
  }

  window.WebSocket = VnetWebSocket;
})();
