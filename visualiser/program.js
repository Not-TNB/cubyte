// Parser + interpreter for the CuBit assembly grammar in
// extension/docs/new-ass-notation.md.
//
// `Interpreter` is a genuine stateful, steppable virtual machine: one
// `.step()` call executes exactly one source line (whatever it does —
// nothing visible for a label, a jump for goto/branch, one or more real
// moves for raw-moves/input/output) against a persistent piece-state
// simulation. This is what backs the line-by-line emulator UI (one source
// line highlighted, a "Run Line" button advances it).
//
// `parseAndBuildTrace` is a convenience wrapper that just drives an
// Interpreter to completion up front, collecting the flat list of moves it
// performs (the "trace") — this is what backs the existing move-by-move
// Next/Prev/Play/Pause playback, which doesn't care about source lines that
// produce no moves.
//
// Scope note on `input`/`output` semantics (see new-ass-notation.md for the
// full writeup, confirmed against codegen.c and extension/examples/*.cubin):
//
// - `output (alg) (pieces)`: `alg` is always the inverse of some register's
//   algorithm and `pieces` its cycle list. Repeatedly applying `alg` until
//   `pieces` are all back home walks that register down to zero one
//   application at a time — the iteration count is exactly its value.
// - `input "prompt"` halts for a number, then loads it into R0 — the fixed,
//   universal I/O register (always present in the manifest, always the same
//   algorithm) — by applying R0's own algorithm that many times. The
//   raw-moves line immediately after `input` in the source is *not* part of
//   it; that's a separate, ordinary instruction (e.g. seeding some other
//   variable) that just happens to come next and must run normally/once.
//   (Confirmed against extension/examples/fib.cubin: treating that line as
//   input's algorithm made R0's corners never get scrambled at all, so the
//   while-loop's solved-check on R0's pieces was always vacuously true and
//   the loop body never ran for *any* input value. Loading into R0 directly
//   instead produces exactly F(n) mod (R0's order) for every n, as expected
//   of a Fibonacci program whose accumulator register has limited order.)

import { parseSequence } from './moves.js';
import { createPieceState, applyMoveToPieces, isCycleSolved, isValidPieceLabel, isPieceHome } from './pieces.js';

const MANIFEST_RE = /^;\s*reg\s+(\d+)\s+alg="([^"]*)"\s+order=(\d+)\s*$/;
const NUMBERED_LINE_RE = /^(\d+)  (\S.*)$/;
const LABEL_RE = /^([A-Za-z_][A-Za-z0-9_]*):$/;
const BRANCH_RE = /^branch cycle\(([^()]*)\) ([A-Za-z_][A-Za-z0-9_]*)$/;
const GOTO_RE = /^goto ([A-Za-z_][A-Za-z0-9_]*)$/;
const INPUT_RE = /^input "([^"\n]*)"$/;
const OUTPUT_RE = /^output \(([^()]*)\) \(([^()]*)\)$/;
const MAX_STEPS = 200000;
const MAX_OUTPUT_ITERATIONS = 10000;

// `row` is the 1-indexed line within the raw program text (used to highlight
// the right line in the UI); `line` is the assembly line-number embedded in
// the syntax itself (e.g. the "3" in "3  goto foo"). They usually coincide
// but `row` is the one that's always correct, even with blank lines, manifest
// comments, or a misnumbered program.
function parseInstruction(content, lineNumber, row, errors) {
  let m;

  if ((m = content.match(LABEL_RE))) {
    return { kind: 'label', name: m[1], line: lineNumber, row };
  }
  if ((m = content.match(BRANCH_RE))) {
    const pieceTokens = m[1].length === 0 ? [] : m[1].split(',');
    for (const token of pieceTokens) {
      if (!isValidPieceLabel(token)) {
        errors.push({ row, message: `Line ${lineNumber}: unknown piece label "${token}"` });
        return null;
      }
    }
    return { kind: 'branch', pieces: pieceTokens, target: m[2], line: lineNumber, row };
  }
  if ((m = content.match(GOTO_RE))) {
    return { kind: 'goto', target: m[1], line: lineNumber, row };
  }
  if ((m = content.match(INPUT_RE))) {
    return { kind: 'input', prompt: m[1], line: lineNumber, row };
  }
  if ((m = content.match(OUTPUT_RE))) {
    const { moves, errors: moveErrors } = parseSequence(m[1]);
    if (moveErrors.length > 0) {
      errors.push({
        row,
        message: `Line ${lineNumber}: invalid move "${moveErrors[0].token}" in output algorithm`,
      });
      return null;
    }

    const pieceTokens = m[2].length === 0 ? [] : m[2].split(',');
    for (const token of pieceTokens) {
      if (!isValidPieceLabel(token)) {
        errors.push({ row, message: `Line ${lineNumber}: unknown piece label "${token}"` });
        return null;
      }
    }

    return { kind: 'output', moves, pieces: pieceTokens, line: lineNumber, row };
  }

  const { moves, errors: moveErrors } = parseSequence(content);
  if (moveErrors.length === 0 && moves.length > 0) {
    return { kind: 'moves', moves, line: lineNumber, row };
  }

  errors.push({ row, message: `Line ${lineNumber}: unrecognized instruction "${content}"` });
  return null;
}

// Splits the raw program text into manifest entries and instructions,
// validating per-line syntax and line-number sequencing.
function parseProgram(text) {
  const rawLines = text.split(/\r\n|\r|\n/);
  const manifest = [];
  const instructions = [];
  const errors = [];
  let expectedLineNumber = 1;

  rawLines.forEach((rawLine, idx) => {
    const row = idx + 1;
    const trimmed = rawLine.trim();
    if (trimmed === '') return;

    if (trimmed.startsWith(';')) {
      const m = trimmed.match(MANIFEST_RE);
      if (m) {
        manifest.push({ index: Number(m[1]), alg: m[2], order: Number(m[3]) });
      } else {
        errors.push({ row, message: `Malformed manifest line: "${rawLine}"` });
      }
      return;
    }

    const lineMatch = rawLine.match(NUMBERED_LINE_RE);
    if (!lineMatch) {
      errors.push({ row, message: `Malformed line (expected "<number>  <instruction>"): "${rawLine}"` });
      return;
    }

    const lineNumber = Number(lineMatch[1]);
    const content = lineMatch[2].replace(/\s+$/, '');

    if (lineNumber !== expectedLineNumber) {
      errors.push({ row, message: `Line ${lineNumber}: expected line number ${expectedLineNumber}` });
    }
    // Continue numbering from whatever is actually on the page, so one
    // misnumbered line doesn't cascade into an error on every line after it.
    expectedLineNumber = lineNumber + 1;

    const instr = parseInstruction(content, lineNumber, row, errors);
    if (instr) instructions.push(instr);
  });

  return { manifest, instructions, errors };
}

// Builds the label -> instruction-index map and validates that every
// goto/branch target resolves and no label is defined twice.
function buildLabelIndex(instructions, errors) {
  const index = new Map();

  instructions.forEach((instr, i) => {
    if (instr.kind !== 'label') return;
    if (index.has(instr.name)) {
      errors.push({ row: null, message: `Duplicate label "${instr.name}" (line ${instr.line})` });
      return;
    }
    index.set(instr.name, i);
  });

  for (const instr of instructions) {
    if ((instr.kind === 'goto' || instr.kind === 'branch') && !index.has(instr.target)) {
      errors.push({ row: null, message: `Line ${instr.line}: target label "${instr.target}" is not defined` });
    }
  }

  return index;
}

// A persistent, steppable execution of a parsed program against a headless
// piece-state simulation (pieces.js). Each step() call runs exactly one
// instruction (one source line) and reports what it did.
class Interpreter {
  constructor(instructions, labelIndex, r0Moves) {
    this.instructions = instructions;
    this.labelIndex = labelIndex;
    this.r0Moves = r0Moves; // register 0's algorithm, parsed once; used by `input`
    this.pieces = createPieceState();
    this.pc = 0;
    this.stepCount = 0;
  }

  get done() {
    return this.pc >= this.instructions.length;
  }

  // The source row of the instruction that will run on the *next* step()
  // call, or null if the program has already finished. Read this before
  // calling step() to know what's about to execute, and after to know
  // what's pending next.
  get currentRow() {
    return this.done ? null : this.instructions[this.pc].row;
  }

  // Runs exactly one instruction. Returns:
  //   { row, moves, log, done, truncated }
  // `row` is the source row of the instruction that just ran. `moves` are
  // real moves to animate, in execution order (empty for label/goto/branch).
  // `log` is a message string or null. `truncated` means the runaway-loop
  // guard tripped and the program was force-stopped.
  step() {
    if (this.done) {
      return { row: null, moves: [], log: null, done: true, truncated: false };
    }

    const instr = this.instructions[this.pc];
    const row = instr.row;

    if (this.stepCount++ > MAX_STEPS) {
      this.pc = this.instructions.length;
      return {
        row,
        moves: [],
        log: `Execution stopped after ${MAX_STEPS} steps (possible infinite loop).`,
        done: true,
        truncated: true,
      };
    }

    const moves = [];
    let log = null;

    if (instr.kind === 'label') {
      this.pc++;
    } else if (instr.kind === 'goto') {
      this.pc = this.labelIndex.get(instr.target);
    } else if (instr.kind === 'branch') {
      const taken = isCycleSolved(this.pieces, instr.pieces);
      this.pc = taken ? this.labelIndex.get(instr.target) : this.pc + 1;
    } else if (instr.kind === 'input') {
      // Genuinely halts (window.prompt blocks the whole tab) until the user
      // answers, then loads the value into R0 by applying R0's own algorithm
      // that many times.
      const entered = typeof window !== 'undefined' && typeof window.prompt === 'function'
        ? window.prompt(instr.prompt, '0')
        : null;

      let value = parseInt(entered, 10);
      let note = '';
      if (entered === null) {
        value = 0;
        note = ' (cancelled, defaulted to 0)';
      } else if (!Number.isInteger(value) || value < 0) {
        value = 0;
        note = ` (invalid value "${entered}", defaulted to 0)`;
      }

      for (let i = 0; i < value; i++) {
        for (const move of this.r0Moves) {
          applyMoveToPieces(this.pieces, move);
          moves.push(move);
        }
      }

      log = `input "${instr.prompt}" -> ${value}${note}`;
      this.pc++;
    } else if (instr.kind === 'output') {
      // Repeatedly apply the instruction's algorithm until its piece list is
      // back home; the iteration count it took is the value being read out.
      let value = 0;
      let gaveUp = false;
      while (!isCycleSolved(this.pieces, instr.pieces)) {
        if (value >= MAX_OUTPUT_ITERATIONS) {
          gaveUp = true;
          break;
        }
        for (const move of instr.moves) {
          applyMoveToPieces(this.pieces, move);
          moves.push(move);
        }
        value++;
      }
      log = gaveUp
        ? `output: gave up after ${MAX_OUTPUT_ITERATIONS} iterations (pieces never solved)`
        : `output: ${value}`;
      this.pc++;
    } else if (instr.kind === 'moves') {
      for (const move of instr.moves) {
        applyMoveToPieces(this.pieces, move);
        moves.push(move);
      }
      this.pc++;
    }

    return { row, moves, log, done: this.done, truncated: false };
  }
}

// Parses `text` and returns either:
//   { errors, manifest }                          (errors non-empty)
//   { interpreter, errors: [], manifest }          (ready to step)
function createInterpreter(text) {
  const { manifest, instructions, errors } = parseProgram(text);
  const labelIndex = buildLabelIndex(instructions, errors);

  if (errors.length === 0 && instructions.length === 0) {
    errors.push({ row: null, message: 'Program contains no instructions.' });
  }

  // R0 is the fixed, universal I/O register and is always present in a
  // well-formed program's manifest; `input` always loads into it.
  let r0Moves = [];
  if (instructions.some((instr) => instr.kind === 'input')) {
    const r0Entry = manifest.find((entry) => entry.index === 0);
    if (!r0Entry) {
      errors.push({ row: null, message: 'Program uses "input" but the manifest has no "reg 0" entry.' });
    } else {
      const { moves, errors: moveErrors } = parseSequence(r0Entry.alg);
      if (moveErrors.length > 0) {
        errors.push({ row: null, message: `Register 0's algorithm "${r0Entry.alg}" contains an invalid move.` });
      } else {
        r0Moves = moves;
      }
    }
  }

  if (errors.length > 0) {
    return { errors, manifest };
  }

  return { interpreter: new Interpreter(instructions, labelIndex, r0Moves), errors: [], manifest };
}

// Convenience wrapper for the move-trace-based playback UI (Next/Prev/Play/
// Pause): eagerly drives an Interpreter to completion and returns the flat
// list of moves it performed (the "trace"), each tagged with its source
// row, plus a log of input/output messages timed to when they occurred.
function parseAndBuildTrace(text) {
  const { interpreter, errors, manifest } = createInterpreter(text);
  if (errors.length > 0) {
    return { trace: [], log: [], errors, manifest };
  }

  const trace = [];
  const log = [];
  while (!interpreter.done) {
    const result = interpreter.step();
    for (const move of result.moves) {
      trace.push({ face: move.face, quarterTurns: move.quarterTurns, raw: move.raw, row: result.row });
    }
    if (result.log) {
      log.push({ afterTraceIndex: trace.length, text: result.log });
    }
  }

  return { trace, log, errors: [], manifest };
}

// Returns the current value of a register given its algorithm (parsed moves),
// its order, and the current piece state.  Works by applying alg^k to a fresh
// solved cube for k = 0..order-1 and finding which k makes the pieces that the
// algorithm moves match the current state.  Returns -1 if no k matches (should
// not happen with a well-formed piece state).
function computeRegisterValue(currentPieces, algMoves, order) {
  // Determine which pieces this algorithm actually moves.
  const probe = createPieceState();
  for (const m of algMoves) applyMoveToPieces(probe, m);
  const movedLabels = probe.filter(p => !isPieceHome(p)).map(p => p.label);

  if (movedLabels.length === 0) return 0;

  const candidate = createPieceState();
  for (let k = 0; k < order; k++) {
    const match = movedLabels.every(label => {
      const ca = candidate.find(p => p.label === label);
      const cu = currentPieces.find(p => p.label === label);
      if (!ca || !cu) return false;
      if (ca.x !== cu.x || ca.y !== cu.y || ca.z !== cu.z) return false;
      for (let i = 0; i < 3; i++)
        for (let j = 0; j < 3; j++)
          if (ca.orientation[i][j] !== cu.orientation[i][j]) return false;
      return true;
    });
    if (match) return k;
    for (const m of algMoves) applyMoveToPieces(candidate, m);
  }
  return -1;
}

// Computes the current value of every register in `manifest` against the given
// piece state.  Returns [{ index, order, value }]; value is null if the
// algorithm string is invalid, -1 if no matching power was found.
function computeAllRegisterValues(pieces, manifest) {
  return manifest.map(entry => {
    const { moves, errors } = parseSequence(entry.alg);
    if (errors.length > 0) return { index: entry.index, order: entry.order, value: null };
    return { index: entry.index, order: entry.order, value: computeRegisterValue(pieces, moves, entry.order) };
  });
}

export { createInterpreter, parseAndBuildTrace, computeAllRegisterValues };
