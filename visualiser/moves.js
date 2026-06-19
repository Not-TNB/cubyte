// Standard cube notation parser. Pure logic, no Three.js dependency.

const FACE_LETTERS = ['U', 'D', 'L', 'R', 'F', 'B'];
const MOVE_RE = /^[UDLRFB](2|')?$/;

// quarterTurns = signed quarter turns of that face, viewed from outside the cube:
// positive = clockwise, negative = counter-clockwise. plain -> 1, "'" -> -1, "2" -> 2.
// (Encoding "'" as -1 rather than "3 CW turns" keeps the animation a quick 90deg
// turn the other way instead of a slow 270deg spin in the same direction.)
function parseMove(token) {
  if (!MOVE_RE.test(token)) {
    return null;
  }
  const face = token[0];
  const modifier = token.slice(1);
  const quarterTurns = modifier === '' ? 1 : modifier === '2' ? 2 : -1;
  return { face, quarterTurns, raw: token };
}

function tokenize(input) {
  return input.trim().split(/\s+/).filter((t) => t.length > 0);
}

// Returns { moves, errors }. errors is a list of { token, index } for tokens
// that failed to parse; moves only contains successfully parsed entries.
// If there are any errors the caller should treat the whole sequence as invalid.
function parseSequence(input) {
  const tokens = tokenize(input);
  const moves = [];
  const errors = [];

  tokens.forEach((token, index) => {
    const move = parseMove(token);
    if (move === null) {
      errors.push({ token, index });
    } else {
      moves.push(move);
    }
  });

  return { moves, errors };
}

export { FACE_LETTERS, parseMove, tokenize, parseSequence };
