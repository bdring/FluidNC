// Netlify Function: fetches a WebUI build (index.html.gz) from a GitHub
// release server-side and returns it unchanged, as application/gzip, for
// the demo's "Install WebUI" menu to upload to FluidNC's LocalFS exactly as
// released.  demo/serve.py implements the same endpoint for local use.
//
// Why this exists: GitHub's release-asset CDN (release-assets.githubusercontent.com)
// sends no Access-Control-Allow-Origin header on these assets, so a browser
// fetch() can't read the response cross-origin. That can't be worked
// around client-side: it has to be fetched server-side (CORS only applies
// to browsers, not this function) and re-served from the demo's own origin.
//
// Deliberately scoped to github.com/<owner>/<repo>/releases/.../index.html.gz
// only (never an arbitrary caller-supplied URL) to avoid this becoming an
// open server-side-request-forgery proxy: owner/repo/tag are validated
// against GitHub's own identifier charset and interpolated into a fixed
// URL template, not accepted as a full URL.

const SAFE_IDENTIFIER = /^[A-Za-z0-9._-]+$/;

// Reads a fetch() Response body incrementally, aborting as soon as `limit`
// is exceeded, instead of buffering the whole thing via arrayBuffer() first
// -- Content-Length is advisory (absent or wrong on a misbehaving/malicious
// response), so the only real enforcement point is while reading.
async function readWithLimit(body, limit) {
  const reader = body.getReader();
  const chunks = [];
  let total = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) {
      break;
    }
    total += value.length;
    if (total > limit) {
      await reader.cancel();
      throw new Error('Upstream asset exceeds size limit');
    }
    chunks.push(value);
  }
  return Buffer.concat(chunks.map((c) => Buffer.from(c)));
}

// A real WebUI build's index.html.gz is well under a megabyte. This cap is
// generous headroom above that, not a tight fit -- its job is only to stop
// a malicious or misconfigured owner/repo/tag from making this public
// endpoint buffer an unbounded asset in Netlify function memory.  (The gzip
// is passed through, never decompressed here, so there is no gzip-bomb
// exposure.)
const MAX_COMPRESSED_BYTES = 10 * 1024 * 1024;

exports.handler = async (event) => {
  const cors = {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, OPTIONS',
  };

  if (event.httpMethod === 'OPTIONS') {
    return { statusCode: 204, headers: cors, body: '' };
  }

  const { owner, repo, tag } = event.queryStringParameters || {};

  if (!owner || !repo || ![owner, repo, tag || 'latest'].every((s) => SAFE_IDENTIFIER.test(s))) {
    return {
      statusCode: 400,
      headers: cors,
      body: 'Usage: ?owner=<github-owner>&repo=<github-repo>[&tag=<release-tag, default latest>]',
    };
  }

  const assetUrl = tag
    ? `https://github.com/${owner}/${repo}/releases/download/${tag}/index.html.gz`
    : `https://github.com/${owner}/${repo}/releases/latest/download/index.html.gz`;

  try {
    const upstream = await fetch(assetUrl);
    if (!upstream.ok) {
      return { statusCode: 502, headers: cors, body: `Upstream fetch failed: ${upstream.status} ${upstream.statusText}` };
    }
    const declaredLength = Number(upstream.headers.get('content-length'));
    if (Number.isFinite(declaredLength) && declaredLength > MAX_COMPRESSED_BYTES) {
      return { statusCode: 502, headers: cors, body: 'Upstream asset exceeds size limit' };
    }
    const compressed = await readWithLimit(upstream.body, MAX_COMPRESSED_BYTES);
    if (compressed.length < 2 || compressed[0] !== 0x1f || compressed[1] !== 0x8b) {
      return { statusCode: 502, headers: cors, body: 'Upstream asset is not gzip data' };
    }
    // Binary bodies go back to Netlify base64-encoded.
    return {
      statusCode: 200,
      headers: { ...cors, 'Content-Type': 'application/gzip' },
      body: compressed.toString('base64'),
      isBase64Encoded: true,
    };
  } catch (err) {
    return { statusCode: 502, headers: cors, body: `Fetch failed: ${err && err.message ? err.message : err}` };
  }
};
