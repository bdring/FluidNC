// Netlify Function: fetches a WebUI build (index.html.gz) from a GitHub
// release server-side and returns it decompressed as text/html.
//
// Why this exists: GitHub's release-asset CDN (release-assets.githubusercontent.com)
// sends neither an Access-Control-Allow-Origin header nor a Content-Encoding
// header on these assets -- a browser fetch() can't read the response
// cross-origin, and even a direct iframe navigation to the URL just
// triggers a file download (Content-Disposition: attachment) instead of
// rendering, since the browser has no reason to gunzip an
// application/octet-stream response. None of that is a browser-side
// restriction that can be worked around client-side: it has to be fetched
// server-side (CORS only applies to browsers, not this function) and
// re-served with the right headers.
//
// Deliberately scoped to github.com/<owner>/<repo>/releases/.../index.html.gz
// only (never an arbitrary caller-supplied URL) to avoid this becoming an
// open server-side-request-forgery proxy: owner/repo/tag are validated
// against GitHub's own identifier charset and interpolated into a fixed
// URL template, not accepted as a full URL.

const zlib = require('zlib');

const SAFE_IDENTIFIER = /^[A-Za-z0-9._-]+$/;

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
    const compressed = Buffer.from(await upstream.arrayBuffer());
    const html = zlib.gunzipSync(compressed).toString('utf-8');
    return {
      statusCode: 200,
      headers: { ...cors, 'Content-Type': 'text/html; charset=utf-8' },
      body: html,
    };
  } catch (err) {
    return { statusCode: 502, headers: cors, body: `Fetch/decompress failed: ${err && err.message ? err.message : err}` };
  }
};
