# FluidNC WASM demo -- build & deploy

The demo (`FluidNC/wasm/demo/`) is a static page: a build step produces
`program.js`/`program.wasm`, which get copied next to `index.html` and
served as plain static files. There is no server-side component.

## 1. One-time setup

Install the Emscripten SDK (needed for `pio run -e wasm`):

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/install latest
~/emsdk/activate latest
```

Each shell session needs it on `PATH` before building:

```bash
source ~/emsdk/emsdk_env.sh
```

(Add that line to your shell rc file to avoid repeating it.)

## 2. Build

From the repo root:

```bash
pio run -e wasm
```

Copy the build output next to the demo's `index.html`:

```bash
cp .pio/build/wasm/program.js FluidNC/wasm/demo/program.js
cp .pio/build/wasm/program.wasm FluidNC/wasm/demo/program.wasm
```

(These two files are gitignored -- they're build output, not source.)

## 3. Test locally

The page needs `Cross-Origin-Opener-Policy`/`Cross-Origin-Embedder-Policy`
headers to use `SharedArrayBuffer` (required for the pthreads build) --
plain `python3 -m http.server` will NOT set these, so use the included
`serve.py` instead:

```bash
cd FluidNC/wasm/demo
python3 serve.py 8767
```

Then open `http://127.0.0.1:8767/index.html`.

### Seed files (native_localfs/native_sd)

The initial LocalFS and SD card contents are staged into the running
instance at boot from `demo/seed-localfs/` and `demo/seed-sd/` -- plain files,
fetched client-side (see `seedFs()` in `index.html`) rather than
embedded in the page, since Netlify (and `serve.py` locally) already
serve everything under `demo/` as static files, same-origin `fetch()`
isn't affected by the COOP/COEP headers above, and a static host can't
be asked to list a directory's contents -- each `seed-*/manifest.json`
does that instead. To add a seed file, drop it under the right
directory and add its relative path to that directory's
`manifest.json`.

Seed files fill in only what is missing: the demo persists
native_localfs, native_sd and native_nvs in the browser's IndexedDB, so a
file the user has edited or uploaded overrides the seed copy, while a
seed file added in a later deploy still appears. The demo page's "Reset
files and settings" button discards the saved copies.

## 4. Deploy to Netlify

One-time: install the Netlify CLI and log in (opens a browser to
authorize):

```bash
npm install -g netlify-cli
netlify login
```

Deploy (from `FluidNC/wasm/`, so both the static site and the function in
`functions/` -- see below -- get picked up together):

```bash
cd FluidNC/wasm
netlify deploy --dir=demo --functions=functions            # draft deploy, prints a one-off preview URL
netlify deploy --dir=demo --functions=functions --prod     # promotes to the site's permanent URL
```

The very first deploy from this folder prints "This folder isn't linked to
a project yet" and an interactive menu:

- **Create & configure a new project** -- makes a brand-new site (fine for
  a first-ever deploy).
- **Link this directory to an existing project** -- pick this to redeploy
  the existing demo site instead of creating a duplicate.

To skip that menu entirely (e.g. from a script, or a fresh checkout that
has no local link to a site you already know the ID of), pass `--site`
directly:

```bash
netlify deploy --site <site-id> --dir=demo --functions=functions --prod
```

Find `<site-id>` in the Netlify dashboard (Site settings -> Site details),
or from a folder that's already linked, in `.netlify/state.json`. The
site's COOP/COEP headers come from `demo/_headers`, which Netlify reads
automatically -- no dashboard configuration needed.

Note: GitHub Pages cannot serve the required COOP/COEP headers (no custom
header support), so it isn't an option for hosting this without extra
tricks (e.g. a service-worker header shim). Netlify, Cloudflare Pages, and
similar hosts that support a `_headers`-style file work out of the box.

## 5. `functions/webui-proxy.js`

Fetches a WebUI build (`index.html.gz`) from a GitHub release server-side
and returns it unchanged as `application/gzip`, for the demo's "Install
WebUI" menu to upload to FluidNC's LocalFS byte-for-byte as released.
`demo/serve.py` implements the same endpoint, so the menu works locally too.

This exists because GitHub's release-asset CDN sends no
`Access-Control-Allow-Origin` header on these assets, so a browser
`fetch()` can't read the response cross-origin.  That can't be worked
around client-side -- it has to be fetched server-side (CORS is a
browser-only restriction) and re-served from the demo's own origin.

```
GET /.netlify/functions/webui-proxy?owner=<github-owner>&repo=<github-repo>[&tag=<release-tag>]
```

`tag` defaults to `latest`. Deliberately scoped to
`github.com/<owner>/<repo>/releases/.../index.html.gz` only -- `owner`/
`repo`/`tag` are validated against GitHub's own identifier charset and
interpolated into a fixed URL template, never accepted as a full URL, so
this can't become an open server-side-request-forgery proxy for arbitrary
URLs.

Example:

```bash
curl -o index.html.gz "https://fluidnc-demo.netlify.app/.netlify/functions/webui-proxy?owner=figamore&repo=FigUI&tag=v1.2.7"
```

It deploys as part of the same `netlify deploy --functions=functions`
command in step 4 above -- no separate deploy step.

## 6. The web server and the demo's virtual network

The wasm module includes FluidNC's own `WebUI/` web server (HTTP routes,
WebSocket, WebDAV), on top of `BrowserAsyncTCP/` -- the AsyncTCP API
carried over numbered "virtual connections" that JS drives with
`fluidnc_vconn_open/send/close` and reads back through
`self.fluidncOnVconnData/Close`.  Everything the demo does with files and
WebUIs goes through that server, the way a browser uses a real board; the
only other ways into the module are the terminal (`fluidnc_send_text`) and
seeding/persisting the emulated filesystems before FluidNC starts
(`Module.FS`).

The demo runs a WebUI the way a controller does.  Its address bar, preset
to `fluidnc.local`, browses that server in the iframe, so any WebUI works
unmodified, exactly as on hardware; with no WebUI installed you get
FluidNC's built-in file manager.  Paths work as on a real board
(`fluidnc.local/?forcefallback=yes`, `fluidnc.local/files?path=/`, ...);
the device answers at `<$Hostname>.local` and `192.168.0.1` (VirtualNet's
address), and other names, `https:` and other ports fail as a browser
would report it.  `demo/?browse=<address>` opens the demo at an address.

"Install WebUI" fetches a build -- a project's latest GitHub release
through `webui-proxy` (see section 5; `serve.py` provides it locally), or
a local file -- and uploads it to LocalFS as `index.html.gz` through
FluidNC's own `/files` route, replacing any `index.html`/`index.html.gz`
already there.  The pieces:

- `demo/vnet-sw.js`: a Service Worker for the whole site that routes each
  request by who made it.  Requests from the device iframe go to FluidNC
  with exactly the paths they have -- the WebUI sees `/`, `/ui/<name>/`,
  `/command`, `/sd/...` as on a controller -- while the demo page and its
  workers use the network.  It decodes gzip (a browser does not decode a
  Response a worker builds), adds the COEP header the isolated page
  requires of a nested document, and inlines `vnet-ws-shim.js` into HTML
  documents.  Because it covers the whole site, the demo must be served
  from the root of its origin.
- `demo/vnet-ws-shim.js`: replaces `window.WebSocket` inside the iframe,
  because Service Workers cannot see WebSockets; each socket becomes a
  MessagePort to the demo page.
- `demo/vnet-host.js`: on the demo page, the HTTP/1.1 and WebSocket client
  that speaks to FluidNC over virtual connections, including a
  path-scoped cookie jar (browsers drop `Set-Cookie` from worker-built
  responses).

The virtual network's traffic is logged to the DevTools console, one line
per HTTP exchange and per WebSocket open/close (`[vnet] PUT /ui/webui2/
preferences2.json -> 201 Created (2.1 KB up, 512 B down, 14 ms)`).
`FluidNCVnet.logFrames = true` in the console adds every WebSocket
message; `FluidNCVnet.log = false` silences it.

Known limits: Telnet is not carried; the WebUI must run inside the demo
page's iframe, not in a tab of its own; requests from a Web Worker a WebUI
starts can't be attributed to its frame and go to the network.

Headless tests (Node, no browser):

```bash
node FluidNC/wasm/vconn_smoke_test.mjs .pio/build/wasm/program.js
node FluidNC/wasm/vnet_client_test.mjs .pio/build/wasm/program.js
node FluidNC/wasm/vnet_unit_test.mjs
```
