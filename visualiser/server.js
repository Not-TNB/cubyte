// Dev server: serves this directory as static files and exposes
// POST /kociemba which shells out to the local ckociemba binary.
// Usage (from anywhere):  node visualiser/server.js
// Then open http://localhost:3000

const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const crypto = require('crypto');
const { execFile } = require('child_process');

const PORT = process.env.PORT || 3000;
const VISUALISER_DIR = __dirname;
const PROJECT_ROOT = path.join(__dirname, '..');
const KOCIEMBA_BIN = path.join(__dirname, '../third_party/ckociemba/bin/kociemba');
const KOCIEMBA_CACHE = path.join(__dirname, '../third_party/ckociemba/cprunetables');
const CUBYTE_BIN = path.join(__dirname, '../cubyte');

// Run `make` (default target) in the project root to (re)build the cubyte
// binary. Callback receives an Error (with stderr attached as .buildStderr)
// on failure, or nothing on success.
function buildCubyte(callback) {
  console.log('Building cubyte (make)...');
  execFile('make', [], { cwd: PROJECT_ROOT }, (err, stdout, stderr) => {
    if (err) {
      console.error('cubyte build failed:\n' + (stderr || err.message));
      err.buildStderr = stderr;
      callback(err);
      return;
    }
    console.log('cubyte build succeeded.');
    callback(null);
  });
}

// Make sure CUBYTE_BIN exists and is executable before we try to run it.
// If it's missing, build it first.
function ensureCubyteBuilt(callback) {
  fs.access(CUBYTE_BIN, fs.constants.X_OK, (err) => {
    if (!err) {
      callback(null);
      return;
    }
    buildCubyte(callback);
  });
}

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js':   'application/javascript; charset=utf-8',
  '.css':  'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.jpg':  'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.png':  'image/png',
  '.ico':  'image/x-icon',
};

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://127.0.0.1:${PORT}`);

  // ----- POST /compile -----
  if (req.method === 'POST' && url.pathname === '/compile') {
    let body = '';
    req.on('data', (chunk) => (body += chunk));
    req.on('end', () => {
      const id = crypto.randomBytes(6).toString('hex');
      const tmpBase = path.join(os.tmpdir(), `cubyte-${id}`);
      const tmpSrc = tmpBase + '.cbyte';
      const tmpOut = tmpBase + '.cubin';

      fs.writeFile(tmpSrc, body, (writeErr) => {
        if (writeErr) {
          res.writeHead(500, { 'Content-Type': 'text/plain' });
          res.end('Failed to write source file');
          return;
        }
        ensureCubyteBuilt((buildErr) => {
          if (buildErr) {
            res.writeHead(500, {
              'Content-Type': 'text/plain',
              'Access-Control-Allow-Origin': '*',
            });
            res.end('Failed to build cubyte:\n' + (buildErr.buildStderr || buildErr.message));
            return;
          }
          execFile(CUBYTE_BIN, [tmpSrc, tmpOut], (err, _stdout, stderr) => {
            fs.unlink(tmpSrc, () => {});
            fs.unlink(tmpBase + '-pp.cbyte', () => {});
            if (err) {
              fs.unlink(tmpOut, () => {});
              res.writeHead(400, {
                'Content-Type': 'text/plain',
                'Access-Control-Allow-Origin': '*',
              });
              res.end(stderr.trim() || `Compilation failed: ${err.message}`);
              return;
            }
            fs.readFile(tmpOut, 'utf8', (readErr, data) => {
              fs.unlink(tmpOut, () => {});
              if (readErr) {
                res.writeHead(500, {
                  'Content-Type': 'text/plain',
                  'Access-Control-Allow-Origin': '*',
                });
                res.end('Failed to read compiler output');
                return;
              }
              res.writeHead(200, {
                'Content-Type': 'text/plain; charset=utf-8',
                'Access-Control-Allow-Origin': '*',
              });
              res.end(data);
            });
          });
        });
      });
    });
    return;
  }

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

server.listen(PORT, '0.0.0.0', () => {
  console.log(`Cubyte visualiser: http://localhost:${PORT}`);
  console.log(`Kociemba binary:   ${KOCIEMBA_BIN}`);
  buildCubyte((err) => {
    if (err) {
      console.warn('Startup build of cubyte failed; will retry on first /compile request.');
    }
  });
});