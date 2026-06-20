// Dev server: serves this directory as static files and exposes
// POST /kociemba which shells out to the local ckociemba binary.
// Usage (from anywhere):  node visualiser/server.js
// Then open http://localhost:3000

const http = require('http');
const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');

const PORT = 3000;
const VISUALISER_DIR = __dirname;
const KOCIEMBA_BIN = path.join(__dirname, '../third_party/ckociemba/bin/kociemba');
const KOCIEMBA_CACHE = path.join(__dirname, '../third_party/ckociemba/cprunetables');

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js':   'application/javascript; charset=utf-8',
  '.css':  'text/css; charset=utf-8',
  '.jpg':  'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.png':  'image/png',
  '.ico':  'image/x-icon',
};

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://127.0.0.1:${PORT}`);

  // ----- POST /kociemba -----
  if (req.method === 'POST' && url.pathname === '/kociemba') {
    let body = '';
    req.on('data', (chunk) => (body += chunk));
    req.on('end', () => {
      const facelets = body.trim();
      if (!/^[URFDLB]{54}$/.test(facelets)) {
        res.writeHead(400, { 'Content-Type': 'text/plain' });
        res.end('invalid facelets');
        return;
      }
      execFile(
        KOCIEMBA_BIN,
        [facelets],
        { env: { ...process.env, CKOCIEMBA_CACHE: KOCIEMBA_CACHE } },
        (err, stdout, stderr) => {
          const out = stdout.trim();
          if (err || !out || out.startsWith('Unsolvable')) {
            res.writeHead(500, { 'Content-Type': 'text/plain' });
            res.end(stderr.trim() || out || String(err));
            return;
          }
          res.writeHead(200, {
            'Content-Type': 'text/plain; charset=utf-8',
            'Access-Control-Allow-Origin': '*',
          });
          res.end(out);
        },
      );
    });
    return;
  }

  // ----- Static files -----
  let urlPath = url.pathname;
  if (urlPath === '/') urlPath = '/index.html';

  const filePath = path.resolve(VISUALISER_DIR, '.' + urlPath);
  if (!filePath.startsWith(VISUALISER_DIR + path.sep) && filePath !== VISUALISER_DIR) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end('Not found');
      return;
    }
    const mime = MIME[path.extname(filePath).toLowerCase()] || 'application/octet-stream';
    res.writeHead(200, { 'Content-Type': mime });
    res.end(data);
  });
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`Cubyte visualiser: http://localhost:${PORT}`);
  console.log(`Kociemba binary:   ${KOCIEMBA_BIN}`);
});
