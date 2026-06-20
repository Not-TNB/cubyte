// Algebraic simplification of a flat Rubik's cube move sequence.
//
// Two reduction rules are applied in alternating passes until the sequence
// stops shrinking:
//
//   1. Same-face collapse: adjacent moves on the same face are merged into
//      their net quarter-turn count (mod 4). Zero-turn results are dropped.
//      e.g.  R R' → ∅,  R R → R2,  R2 R' → R
//
//   2. Opposite-face sort: moves on opposite faces (R/L, U/D, F/B) commute,
//      so adjacent opposite-face pairs are bubble-sorted to a canonical order
//      (alphabetically earlier face first). This brings same-face moves that
//      were separated only by their opposite into adjacency, where rule 1 can
//      then cancel them.
//      e.g.  R L R  →  L R R  →  L R2

const OPPOSITE = { U: 'D', D: 'U', R: 'L', L: 'R', F: 'B', B: 'F' };

function norm(t) {
  return ((t % 4) + 4) % 4;
}

function toRaw(face, t) {
  return face + (t === 1 ? '' : t === 2 ? '2' : "'");
}

export function simplifyTrace(moves) {
  let seq = moves
    .map((m) => ({ face: m.face, t: norm(m.quarterTurns) }))
    .filter((m) => m.t !== 0);

  let prevLen = -1;
  while (seq.length !== prevLen) {
    prevLen = seq.length;

    // Pass 1 — stack-based same-face collapse
    const out = [];
    for (const mv of seq) {
      const top = out.length ? out[out.length - 1] : null;
      if (top && top.face === mv.face) {
        const t = norm(top.t + mv.t);
        if (t) top.t = t;
        else out.pop();
      } else {
        out.push({ ...mv });
      }
    }
    seq = out;

    // Pass 2 — bubble opposite-face pairs to canonical (alphabetical) order
    let swapped = true;
    while (swapped) {
      swapped = false;
      for (let i = 0; i < seq.length - 1; i++) {
        if (OPPOSITE[seq[i].face] === seq[i + 1].face && seq[i].face > seq[i + 1].face) {
          [seq[i], seq[i + 1]] = [seq[i + 1], seq[i]];
          swapped = true;
        }
      }
    }
  }

  return seq.map(({ face, t }) => ({
    face,
    quarterTurns: t === 3 ? -1 : t,
    raw: toRaw(face, t),
  }));
}
