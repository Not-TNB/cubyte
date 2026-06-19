// Headless (no Three.js) simulation of the 20 named corner/edge pieces, used
// by the program interpreter to evaluate `branch cycle(...)` conditions while
// building a move trace, before any animation happens. Position math reuses
// cubeMath.js so it can never disagree with what the renderer actually does;
// orientation is tracked as a 3x3 integer rotation matrix (entries always
// exactly -1, 0, or 1, since every reachable orientation is one of the 24
// proper rotations of a cube).

import { AXIS, LAYER, stepsFor, rotateVectorStep } from './cubeMath.js';

// Home (solved) position for each of the 20 movable pieces, named per the
// grammar's piece-label set (extension/docs/new-ass-notation.md). Corners
// have all three coordinates non-zero; edges have exactly one zero
// coordinate (the axis they have no sticker on).
const PIECES = [
  { label: 'UFL', x: -1, y: 1, z: 1 },
  { label: 'UFR', x: 1, y: 1, z: 1 },
  { label: 'UBL', x: -1, y: 1, z: -1 },
  { label: 'UBR', x: 1, y: 1, z: -1 },
  { label: 'DFL', x: -1, y: -1, z: 1 },
  { label: 'DFR', x: 1, y: -1, z: 1 },
  { label: 'DBL', x: -1, y: -1, z: -1 },
  { label: 'DBR', x: 1, y: -1, z: -1 },
  { label: 'UF', x: 0, y: 1, z: 1 },
  { label: 'UB', x: 0, y: 1, z: -1 },
  { label: 'DF', x: 0, y: -1, z: 1 },
  { label: 'DB', x: 0, y: -1, z: -1 },
  { label: 'UL', x: -1, y: 1, z: 0 },
  { label: 'UR', x: 1, y: 1, z: 0 },
  { label: 'DL', x: -1, y: -1, z: 0 },
  { label: 'DR', x: 1, y: -1, z: 0 },
  { label: 'FL', x: -1, y: 0, z: 1 },
  { label: 'FR', x: 1, y: 0, z: 1 },
  { label: 'BL', x: -1, y: 0, z: -1 },
  { label: 'BR', x: 1, y: 0, z: -1 },
];

const PIECE_LABELS = new Set(PIECES.map((p) => p.label));

const IDENTITY = [
  [1, 0, 0],
  [0, 1, 0],
  [0, 0, 1],
];

// The 3x3 matrix form of cubeMath.rotateVectorStep's +90deg transform, for
// composing orientation. Must stay consistent with rotateVectorStep — both
// represent the same physical +90deg rotation about each axis.
const BASE_MATRIX = {
  x: [
    [1, 0, 0],
    [0, 0, -1],
    [0, 1, 0],
  ],
  y: [
    [0, 0, 1],
    [0, 1, 0],
    [-1, 0, 0],
  ],
  z: [
    [0, -1, 0],
    [1, 0, 0],
    [0, 0, 1],
  ],
};

function matMul(a, b) {
  const r = [
    [0, 0, 0],
    [0, 0, 0],
    [0, 0, 0],
  ];
  for (let i = 0; i < 3; i++) {
    for (let j = 0; j < 3; j++) {
      r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    }
  }
  return r;
}

function matEquals(a, b) {
  for (let i = 0; i < 3; i++) {
    for (let j = 0; j < 3; j++) {
      if (a[i][j] !== b[i][j]) return false;
    }
  }
  return true;
}

function isValidPieceLabel(label) {
  return PIECE_LABELS.has(label);
}

// Returns a fresh array of 20 pieces in the solved state.
function createPieceState() {
  return PIECES.map((p) => ({
    label: p.label,
    homeX: p.x,
    homeY: p.y,
    homeZ: p.z,
    x: p.x,
    y: p.y,
    z: p.z,
    orientation: IDENTITY,
  }));
}

// Applies one move to the piece state in place.
function applyMoveToPieces(pieces, move) {
  const axis = AXIS[move.face];
  const steps = stepsFor(move);
  if (steps === 0) return;

  const [axisKey, value] = LAYER[move.face];
  let stepMatrix = IDENTITY;
  for (let i = 0; i < steps; i++) {
    stepMatrix = matMul(BASE_MATRIX[axis], stepMatrix);
  }

  for (const piece of pieces) {
    if (piece[axisKey] !== value) continue;
    for (let i = 0; i < steps; i++) {
      rotateVectorStep(piece, axis);
    }
    piece.orientation = matMul(stepMatrix, piece.orientation);
  }
}

// A piece is "home" iff it occupies its solved position with no net twist.
function isPieceHome(piece) {
  return (
    piece.x === piece.homeX &&
    piece.y === piece.homeY &&
    piece.z === piece.homeZ &&
    matEquals(piece.orientation, IDENTITY)
  );
}

// Empty `labels` means "whole cube solved" (all 20 pieces home).
function isCycleSolved(pieces, labels) {
  const targets = labels.length > 0 ? labels : PIECES.map((p) => p.label);
  return targets.every((label) => {
    const piece = pieces.find((p) => p.label === label);
    return piece !== undefined && isPieceHome(piece);
  });
}

export { PIECE_LABELS, isValidPieceLabel, createPieceState, applyMoveToPieces, isPieceHome, isCycleSolved };
