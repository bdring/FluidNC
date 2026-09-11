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
and returns it decompressed as `text/html`, so it can be loaded into an
iframe from the browser.

This exists because GitHub's release-asset CDN sends neither an
`Access-Control-Allow-Origin` header nor a `Content-Encoding` header on
these assets: a browser `fetch()` can't read the response cross-origin,
and even navigating an iframe straight to the URL just triggers a file
download (`Content-Disposition: attachment`) instead of rendering, since
the browser has no reason to gunzip an `application/octet-stream`
response. None of that can be worked around client-side -- it has to be
fetched server-side (CORS is a browser-only restriction) and re-served
with the right headers.

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
curl "https://fluidnc-demo.netlify.app/.netlify/functions/webui-proxy?owner=figamore&repo=FigUI&tag=v1.2.7"
```

It deploys as part of the same `netlify deploy --functions=functions`
command in step 4 above -- no separate deploy step.
