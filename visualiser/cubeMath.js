// Pure rotation math shared by the Three.js renderer (cube.js) and the
// headless piece-state simulator used by the program interpreter (pieces.js).
// Keeping one copy of this math means the interpreter's branch evaluation can
// never silently disagree with what actually gets rendered.

// Coordinate frame: +X = right (R), +Y = up (U), +Z = towards viewer/front (F).
//
// Sign convention derivation: a clockwise turn of a face (as seen by an
// observer standing outside that face looking back at the cube) is a
// NEGATIVE right-hand-rule rotation about the world axis when the face's
// outward normal is the *positive* end of that axis (R/U/F), and a POSITIVE
// rotation when the outward normal is the *negative* end (L/D/B). This falls
// out of the standard fact that a positive (right-hand-rule) rotation about
// an axis looks counter-clockwise to an observer on the positive end of that
// axis looking back toward the origin.
const AXIS = { U: 'y', D: 'y', L: 'x', R: 'x', F: 'z', B: 'z' };
const SIGN = { U: -1, D: 1, L: 1, R: -1, F: -1, B: 1 };

// Which logical-grid coordinate selects the 9 cubies/pieces in a face's layer.
const LAYER = {
  U: ['y', 1],
  D: ['y', -1],
  L: ['x', -1],
  R: ['x', 1],
  F: ['z', 1],
  B: ['z', -1],
};

// Net signed turn angle for a move, given quarterTurns where positive = CW
// and negative = CCW as viewed from outside the face (see moves.js).
function angleDegFor(move) {
  return SIGN[move.face] * move.quarterTurns * 90;
}

// Number of forward +90deg (right-hand-rule positive) steps equivalent to a
// move's net angle, normalized to 0..3.
function stepsFor(move) {
  const raw = Math.round(angleDegFor(move) / 90);
  return ((raw % 4) + 4) % 4;
}

// One +90deg (right-hand-rule positive) rotation step applied to the two
// coordinates perpendicular to `axis`, mutating `v` in place. `v` must have
// numeric x, y, z fields. These three formulas are the standard cyclic
// rotation for a positive quarter turn about each axis.
function rotateVectorStep(v, axis) {
  if (axis === 'x') {
    const { y, z } = v;
    v.y = -z;
    v.z = y;
  } else if (axis === 'y') {
    const { x, z } = v;
    v.z = -x;
    v.x = z;
  } else {
    const { x, y } = v;
    v.x = -y;
    v.y = x;
  }
}

export { AXIS, SIGN, LAYER, angleDegFor, stepsFor, rotateVectorStep };
