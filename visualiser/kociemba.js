// Bridge between the headless piece-state simulator and the local kociemba
// server.  Call traceToFacelets(trace) to turn a move trace into the 54-char
// URFDLB facelet string, then kociembaSimplify(facelets) to fetch the optimal
// move sequence from the server (inverted so it *produces* the state rather
// than solving it).  Falls back gracefully — callers should catch.

import { createPieceState, applyMoveToPieces } from './pieces.js';

// Kociemba 54-facelet layout: U(0-8) R(9-17) F(18-26) D(27-35) L(36-44) B(45-53)
// Each non-center slot: { x, y, z, n: [nx,ny,nz] } where n is the outward
// world-space normal.  Centers (slots 4,13,22,31,40,49) are null and filled
// from CENTER_COLOR below.
//
// Row order matches cube_to_kociemba_facelets() in src/kociemba.c:
//   U: back→front, L→R       R: front→back, top→bottom
//   F: L→R, top→bottom       D: front→back, L→R
//   L: back→front, top→bot   B: R→L (mirrored), top→bottom
const FACELET_MAP = [
  // U face  (y=1, normal [0,1,0])
  {x:-1,y:1,z:-1,n:[0,1,0]},{x:0,y:1,z:-1,n:[0,1,0]},{x:1,y:1,z:-1,n:[0,1,0]},
  {x:-1,y:1,z:0, n:[0,1,0]},null,                      {x:1,y:1,z:0, n:[0,1,0]},
  {x:-1,y:1,z:1, n:[0,1,0]},{x:0,y:1,z:1, n:[0,1,0]}, {x:1,y:1,z:1, n:[0,1,0]},
  // R face  (x=1, normal [1,0,0])
  {x:1,y:1, z:1, n:[1,0,0]},{x:1,y:1, z:0,n:[1,0,0]},{x:1,y:1, z:-1,n:[1,0,0]},
  {x:1,y:0, z:1, n:[1,0,0]},null,                      {x:1,y:0, z:-1,n:[1,0,0]},
  {x:1,y:-1,z:1, n:[1,0,0]},{x:1,y:-1,z:0,n:[1,0,0]},{x:1,y:-1,z:-1,n:[1,0,0]},
  // F face  (z=1, normal [0,0,1])
  {x:-1,y:1, z:1,n:[0,0,1]},{x:0,y:1, z:1,n:[0,0,1]},{x:1,y:1, z:1,n:[0,0,1]},
  {x:-1,y:0, z:1,n:[0,0,1]},null,                      {x:1,y:0, z:1,n:[0,0,1]},
  {x:-1,y:-1,z:1,n:[0,0,1]},{x:0,y:-1,z:1,n:[0,0,1]},{x:1,y:-1,z:1,n:[0,0,1]},
  // D face  (y=-1, normal [0,-1,0])
  {x:-1,y:-1,z:1, n:[0,-1,0]},{x:0,y:-1,z:1, n:[0,-1,0]},{x:1,y:-1,z:1, n:[0,-1,0]},
  {x:-1,y:-1,z:0, n:[0,-1,0]},null,                       {x:1,y:-1,z:0, n:[0,-1,0]},
  {x:-1,y:-1,z:-1,n:[0,-1,0]},{x:0,y:-1,z:-1,n:[0,-1,0]},{x:1,y:-1,z:-1,n:[0,-1,0]},
  // L face  (x=-1, normal [-1,0,0])
  {x:-1,y:1, z:-1,n:[-1,0,0]},{x:-1,y:1, z:0,n:[-1,0,0]},{x:-1,y:1, z:1,n:[-1,0,0]},
  {x:-1,y:0, z:-1,n:[-1,0,0]},null,                       {x:-1,y:0, z:1,n:[-1,0,0]},
  {x:-1,y:-1,z:-1,n:[-1,0,0]},{x:-1,y:-1,z:0,n:[-1,0,0]},{x:-1,y:-1,z:1,n:[-1,0,0]},
  // B face  (z=-1, normal [0,0,-1]) — columns run R→L when viewed from outside
  {x:1, y:1, z:-1,n:[0,0,-1]},{x:0,y:1, z:-1,n:[0,0,-1]},{x:-1,y:1, z:-1,n:[0,0,-1]},
  {x:1, y:0, z:-1,n:[0,0,-1]},null,                        {x:-1,y:0, z:-1,n:[0,0,-1]},
  {x:1, y:-1,z:-1,n:[0,0,-1]},{x:0,y:-1,z:-1,n:[0,0,-1]},{x:-1,y:-1,z:-1,n:[0,0,-1]},
];

// Center slot index → face color.
const CENTER_COLOR = { 4:'U', 13:'R', 22:'F', 31:'D', 40:'L', 49:'B' };

// Facelet string for a fully-solved cube (no inversion needed; skip server call).
const SOLVED_FACELETS = 'U'.repeat(9) + 'R'.repeat(9) + 'F'.repeat(9) +
                        'D'.repeat(9) + 'L'.repeat(9) + 'B'.repeat(9);

// Apply the piece's orientation (as M^T * n) to find which home face a sticker
// came from, then map that direction to a face color character.
function stickerColor(orientation, nx, ny, nz) {
  const M = orientation;
  const hdx = M[0][0]*nx + M[1][0]*ny + M[2][0]*nz;
  const hdy = M[0][1]*nx + M[1][1]*ny + M[2][1]*nz;
  const hdz = M[0][2]*nx + M[1][2]*ny + M[2][2]*nz;
  if (hdx >  0.5) return 'R';
  if (hdx < -0.5) return 'L';
  if (hdy >  0.5) return 'U';
  if (hdy < -0.5) return 'D';
  if (hdz >  0.5) return 'F';
  return 'B';
}

// Convert a piece-state array (from pieces.js) to a 54-char kociemba string.
export function piecesToFacelets(pieces) {
  const out = [];
  for (let i = 0; i < 54; i++) {
    const f = FACELET_MAP[i];
    if (!f) { out.push(CENTER_COLOR[i]); continue; }
    const piece = pieces.find(p => p.x === f.x && p.y === f.y && p.z === f.z);
    out.push(stickerColor(piece.orientation, ...f.n));
  }
  return out.join('');
}

// Simulate trace on a fresh piece state and return the facelet string.
export function traceToFacelets(trace) {
  const pieces = createPieceState();
  for (const move of trace) applyMoveToPieces(pieces, move);
  return piecesToFacelets(pieces);
}

// Parse a kociemba solution string ("R U R' U' R2") into move objects.
function parseSolution(str) {
  return str.trim().split(/\s+/).filter(Boolean).map(token => {
    const face = token[0];
    const suffix = token.slice(1);
    const quarterTurns = suffix === "'" ? -1 : suffix === '2' ? 2 : 1;
    return { face, quarterTurns, raw: token };
  });
}

// Invert: reverse the sequence and flip each move's direction (R→R', R2→R2).
function invertMoves(moves) {
  return moves.slice().reverse().map(m => {
    const qt = m.quarterTurns === 2 ? 2 : -m.quarterTurns;
    const raw = qt === 1 ? m.face : qt === -1 ? m.face + "'" : m.face + '2';
    return { face: m.face, quarterTurns: qt, raw };
  });
}

// POST facelets to the local kociemba server and return the optimal sequence
// that *produces* the state (i.e. the inverse of kociemba's solution).
// Throws if the server is unreachable or returns an error.
export async function kociembaSimplify(facelets) {
  if (facelets === SOLVED_FACELETS) return [];
  const resp = await fetch('/kociemba', {
    method: 'POST',
    headers: { 'Content-Type': 'text/plain' },
    body: facelets,
  });
  if (!resp.ok) throw new Error(await resp.text());
  const solution = await resp.text();
  if (!solution) return [];
  return invertMoves(parseSolution(solution));
}
