import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { createCube, disposeCube, applyMove } from './cube.js';
import { parseAndBuildTrace, createInterpreter, computeAllRegisterValues } from './program.js';
import { createPieceState, applyMoveToPieces } from './pieces.js';
import { simplifyTrace } from './simplify.js';
import { traceToFacelets, kociembaSimplify } from './kociemba.js';

// ---------------------------------------------------------------------------
// DOM references
// ---------------------------------------------------------------------------

const container = document.getElementById('scene-container');
const programInput = document.getElementById('program-input');
const cubyteEditor = document.getElementById('cubyte-editor');
const cubyteLineNumbers = document.getElementById('cubyte-line-numbers');
const cubyteInput = document.getElementById('cubyte-input');
const modeSelect = document.getElementById('mode-select');
const codeView = document.getElementById('code-view');
const registerView = document.getElementById('register-view');
const runBtn = document.getElementById('run-btn');
const runSimplifiedBtn = document.getElementById('run-simplified-btn');
const runLineBtn = document.getElementById('run-line-btn');
const programError = document.getElementById('program-error');
const programLog = document.getElementById('program-log');
const moveDisplay = document.getElementById('move-display');
const resetBtn = document.getElementById('reset-btn');
const prevBtn = document.getElementById('prev-btn');
const playPauseBtn = document.getElementById('play-pause-btn');
const nextBtn = document.getElementById('next-btn');
const skipBtn = document.getElementById('skip-btn');
const speedSlider = document.getElementById('speed-slider');
const sidebarEl = document.getElementById('sidebar');
const sidebarResizer = document.getElementById('sidebar-resizer');
const moveDisplayResizer = document.getElementById('move-display-resizer');

// ---------------------------------------------------------------------------
// Scene / camera / renderer / controls
// ---------------------------------------------------------------------------

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x1b1d22);

const camera = new THREE.PerspectiveCamera(45, container.clientWidth / container.clientHeight, 0.1, 100);
camera.position.set(4.5, 4.5, 6);
camera.lookAt(0, 0, 0);

const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
renderer.setPixelRatio(window.devicePixelRatio);
renderer.setSize(container.clientWidth, container.clientHeight);
renderer.outputColorSpace = THREE.SRGBColorSpace;
container.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.minDistance = 4;
controls.maxDistance = 16;

scene.add(new THREE.AmbientLight(0xffffff, 0.6));
const keyLight = new THREE.DirectionalLight(0xffffff, 0.8);
keyLight.position.set(5, 8, 6);
scene.add(keyLight);
const fillLight = new THREE.DirectionalLight(0xffffff, 0.3);
fillLight.position.set(-5, -3, -4);
scene.add(fillLight);

function resizeRenderer() {
  camera.aspect = container.clientWidth / container.clientHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(container.clientWidth, container.clientHeight);
}

window.addEventListener('resize', resizeRenderer);

function renderLoop() {
  requestAnimationFrame(renderLoop);
  controls.update();
  renderer.render(scene, camera);
}
renderLoop();

// ---------------------------------------------------------------------------
// Playback state machine
// ---------------------------------------------------------------------------

let cubies = createCube(scene);
let parsedMoves = []; // the flat move trace produced by program.js
let log = []; // [{ afterTraceIndex, text }]
let currentIndex = 0; // number of moves already applied to the cube
let isPlaying = false;
let isAnimating = false;
let cancelCurrentMove = null; // cancel handle for the in-flight applyMove, if any
let isSolving = false;    // true while awaiting the kociemba server
let isCompiling = false;  // true while awaiting the /compile endpoint

// Synchronously snaps and cleans up the in-flight animation so rebuildCube()
// can safely dispose the scene without leaving orphaned pivot children.
function abortAnimation() {
  if (cancelCurrentMove) {
    const fn = cancelCurrentMove;
    cancelCurrentMove = null;
    fn();
  }
}
// Bumped by runProgram()/resetCube() so an in-flight stepNext/stepPrev that
// was started by a previous program run can detect it's been superseded and
// quietly abandon its post-animation bookkeeping instead of corrupting the
// freshly-reset state.
let generation = 0;

// Register view state — manifest from the last program parsed, a piece state
// tracking the trace-mode cube (null when not tracking), and the last known
// good value per register (used as fallback when the state can't be decoded).
let currentManifest = [];
let tracePieces = null;
let lastRegisterValues = new Map(); // register index -> last known value

// Line-by-line emulator state. Mutually exclusive with the trace-based
// playback above — starting one mode tears down the other (stopLineEmulator
// / runProgram below). `lineInterpreter` is null whenever this mode isn't
// active.
let lineInterpreter = null;
let codeLines = []; // one <div> per raw source line, for highlighting

function rebuildCube() {
  disposeCube(scene, cubies);
  cubies = createCube(scene);
}

function renderMoveDisplay() {
  moveDisplay.innerHTML = '';
  parsedMoves.forEach((move, i) => {
    const span = document.createElement('span');
    span.textContent = move.raw;
    if (i < currentIndex) span.classList.add('done');
    if (i === currentIndex) span.classList.add('active');
    moveDisplay.appendChild(span);
  });
}

function renderLog() {
  programLog.textContent = log
    .filter((entry) => entry.afterTraceIndex <= currentIndex)
    .map((entry) => entry.text)
    .join('\n');
}

function renderRegisterView() {
  if (currentManifest.length === 0) {
    registerView.innerHTML = '';
    return;
  }

  const pieces = lineInterpreter ? lineInterpreter.pieces : tracePieces;
  const raw = pieces !== null
    ? computeAllRegisterValues(pieces, currentManifest)
    : currentManifest.map(e => ({ index: e.index, order: e.order, value: null }));

  const values = raw.map(({ index, order, value }) => {
    if (value !== null && value >= 0) {
      lastRegisterValues.set(index, value);
      return { index, order, display: String(value), pct: value / order * 100 };
    }
    const cached = lastRegisterValues.get(index);
    if (cached !== undefined) {
      return { index, order, display: String(cached), pct: cached / order * 100 };
    }
    return { index, order, display: value === null ? '—' : '?', pct: 0 };
  });

  registerView.innerHTML = '<div class="register-header">Registers</div>' +
    values.map(({ index, order, display, pct }) =>
      `<div class="register-row">
        <span class="register-label">R${index}</span>
        <span class="register-value">${display} / ${order}</span>
        <div class="register-bar-container"><div class="register-bar-fill" style="width:${pct.toFixed(1)}%"></div></div>
      </div>`
    ).join('');
}

function updateButtonStates() {
  const hasMoves = parsedMoves.length > 0;
  const atEnd = currentIndex >= parsedMoves.length;
  prevBtn.disabled = !hasMoves || currentIndex === 0 || isAnimating;
  nextBtn.disabled = !hasMoves || atEnd || isAnimating;
  skipBtn.disabled = !hasMoves || atEnd || isAnimating;
  playPauseBtn.disabled = !hasMoves || (atEnd && !isPlaying);
  // Reset must stay clickable while the line emulator is active (it's the
  // only way back to editing) even though it never populates parsedMoves.
  resetBtn.disabled = !hasMoves && !lineInterpreter;
  const busy = isCompiling || isSolving || isPlaying;
  runBtn.disabled = busy;
  runSimplifiedBtn.disabled = busy;
  runLineBtn.disabled = busy || isAnimating || modeSelect.value === 'cubyte' || (lineInterpreter !== null && lineInterpreter.done);
  if (lineInterpreter && !lineInterpreter.done) {
    runLineBtn.textContent = 'Run Line ' + lineInterpreter.currentRow;
  } else {
    runLineBtn.textContent = 'Run Line';
  }
}

function pausePlayback() {
  isPlaying = false;
  playPauseBtn.innerHTML = '<span class="material-icons">play_arrow</span>';
}

// ---------------------------------------------------------------------------
// Line-by-line emulator
// ---------------------------------------------------------------------------

function showCodeView(text) {
  codeView.innerHTML = '';
  codeLines = text.split(/\r\n|\r|\n/).map((rawLine) => {
    const div = document.createElement('div');
    div.className = 'code-line';
    // A blank line would collapse to zero height and be unselectable/hard
    // to see; keep it visible as an empty-looking but real line.
    div.textContent = rawLine.length > 0 ? rawLine : ' ';
    codeView.appendChild(div);
    return div;
  });
  programInput.hidden = true;
  codeView.hidden = false;
}

function updateLineNumbers() {
  const count = cubyteInput.value.split('\n').length;
  cubyteLineNumbers.innerHTML = Array.from(
    { length: count },
    (_, i) => `<div>${i + 1}</div>`
  ).join('');
  cubyteLineNumbers.scrollTop = cubyteInput.scrollTop;
}

function showTextarea() {
  const cubyte = modeSelect.value === 'cubyte';
  programInput.hidden = cubyte;
  cubyteEditor.hidden = !cubyte;
  codeView.hidden = true;
}

// POST CuByte source to the server and return the compiled assembly text.
// Throws with the compiler's error output if compilation fails.
async function compileSource() {
  const resp = await fetch('/compile', {
    method: 'POST',
    headers: { 'Content-Type': 'text/plain' },
    body: cubyteInput.value,
  });
  if (!resp.ok) throw new Error(await resp.text());
  return resp.text();
}

// Returns the assembly text to run: either the textarea directly (assembly
// mode) or freshly compiled from the server (CuByte mode).
async function getAssemblyText() {
  if (modeSelect.value !== 'cubyte') return programInput.value;
  isCompiling = true;
  const prevText = runBtn.textContent;
  runBtn.textContent = 'Compiling…';
  updateButtonStates();
  try {
    return await compileSource();
  } finally {
    isCompiling = false;
    runBtn.textContent = prevText;
    updateButtonStates();
  }
}

function highlightRow(row) {
  codeLines.forEach((div) => div.classList.remove('current'));
  if (row !== null && codeLines[row - 1]) {
    codeLines[row - 1].classList.add('current');
    codeLines[row - 1].scrollIntoView({ block: 'nearest' });
  }
}

function appendProgramLog(text) {
  programLog.textContent += (programLog.textContent ? '\n' : '') + text;
}

// Tears down an active line-emulator session and goes back to showing the
// editable textarea. Called whenever the trace-based Run/Reset controls are
// used, since the two modes share the same cube/playback state and must not
// run concurrently.
function stopLineEmulator() {
  if (!lineInterpreter) return;
  lineInterpreter = null;
  highlightRow(null);
  showTextarea();
  renderRegisterView();
}

// First click: parses the program, shows the read-only highlighted view, and
// highlights the first line — without running it yet, so there's always a
// "currently pending" line visible before anything executes.
// Every click after that: runs exactly the highlighted line, animates
// whatever moves it produced, logs any input/output message, and moves the
// highlight to wherever execution now points (which may jump, for
// goto/branch).
async function runLine() {
  if (isAnimating) return;

  if (!lineInterpreter) {
    programError.textContent = '';
    let assemblyText;
    try {
      assemblyText = await getAssemblyText();
    } catch (e) {
      programError.textContent = e.message;
      return;
    }
    const init = createInterpreter(assemblyText);
    if (init.errors.length > 0) {
      programError.textContent = init.errors.map((e) => e.message).join('\n');
      return;
    }

    pausePlayback();
    generation++;
    isAnimating = false;
    parsedMoves = [];
    log = [];
    currentIndex = 0;
    rebuildCube();
    renderMoveDisplay();
    programLog.textContent = '';

    currentManifest = init.manifest || [];
    lastRegisterValues = new Map();
    tracePieces = null;
    lineInterpreter = init.interpreter;
    showCodeView(assemblyText);
    highlightRow(lineInterpreter.currentRow);
    renderRegisterView();
    updateButtonStates();
    return;
  }

  if (lineInterpreter.done) return;

  const myGeneration = generation;
  isAnimating = true;
  updateButtonStates();

  const durationMs = 1230 - Number(speedSlider.value);
  const result = lineInterpreter.step();
  for (const move of result.moves) {
    const { promise, cancel } = applyMove(scene, cubies, move, durationMs);
    cancelCurrentMove = cancel;
    await promise;
    cancelCurrentMove = null;
    if (myGeneration !== generation) return;
  }
  if (myGeneration !== generation) return;

  if (result.log) appendProgramLog(result.log);
  if (result.done) appendProgramLog('Program finished.');
  highlightRow(lineInterpreter.currentRow);
  renderRegisterView();

  isAnimating = false;
  updateButtonStates();
}

async function runSimplifiedProgram() {
  programError.textContent = '';
  abortAnimation();
  stopLineEmulator();

  let assemblyText;
  try {
    assemblyText = await getAssemblyText();
  } catch (e) {
    programError.textContent = e.message;
    return;
  }

  const result = parseAndBuildTrace(assemblyText);
  if (result.errors.length > 0) {
    programError.textContent = result.errors.map((e) => e.message).join('\n');
    return;
  }

  const originalCount = result.trace.length;
  isSolving = true;
  runSimplifiedBtn.textContent = 'Solving…';
  updateButtonStates();

  let simplified;
  let method;
  try {
    const facelets = traceToFacelets(result.trace);
    simplified = await kociembaSimplify(facelets);
    method = 'Kociemba';
  } catch (_) {
    simplified = simplifyTrace(result.trace);
    method = 'Algebraic';
  } finally {
    isSolving = false;
    runSimplifiedBtn.textContent = 'Run Simplified';
    updateButtonStates();
  }

  const simplifiedCount = simplified.length;

  pausePlayback();
  generation++;
  isAnimating = false;
  parsedMoves = simplified;
  // Show all I/O entries immediately (simplified moves don't share trace indices
  // with the original program), then append the simplification summary.
  log = [
    ...result.log.map((e) => ({ afterTraceIndex: 0, text: e.text })),
    { afterTraceIndex: 0, text: `${method}: ${originalCount} → ${simplifiedCount} moves` },
  ];
  currentIndex = 0;
  currentManifest = result.manifest || [];
  lastRegisterValues = new Map();
  tracePieces = null; // simplified moves are synthetic — register values not tracked
  rebuildCube();
  renderMoveDisplay();
  renderLog();
  renderRegisterView();
  updateButtonStates();

  togglePlay();
}

async function runProgram() {
  programError.textContent = '';
  abortAnimation();
  stopLineEmulator();

  let assemblyText;
  try {
    assemblyText = await getAssemblyText();
  } catch (e) {
    programError.textContent = e.message;
    return;
  }

  const result = parseAndBuildTrace(assemblyText);
  if (result.errors.length > 0) {
    programError.textContent = result.errors.map((e) => e.message).join('\n');
    return;
  }

  pausePlayback();
  generation++;
  isAnimating = false;
  parsedMoves = result.trace;
  log = result.log;
  currentIndex = 0;
  currentManifest = result.manifest || [];
  lastRegisterValues = new Map();
  tracePieces = createPieceState();
  rebuildCube();
  renderMoveDisplay();
  renderLog();
  renderRegisterView();
  updateButtonStates();

  // "Run" steps through every move automatically; Pause/Next/Prev/Reset
  // remain available to interrupt or scrub once it's going.
  togglePlay();
}

async function stepNext() {
  if (isAnimating || currentIndex >= parsedMoves.length) return;
  const myGeneration = generation;
  isAnimating = true;
  updateButtonStates();

  const durationMs = 1230 - Number(speedSlider.value);
  const { promise, cancel } = applyMove(scene, cubies, parsedMoves[currentIndex], durationMs);
  cancelCurrentMove = cancel;
  await promise;
  cancelCurrentMove = null;
  // A Run/Reset click during the await already reset isAnimating/currentIndex
  // for the new program; don't stomp on that with this stale call's result.
  if (myGeneration !== generation) return;
  if (tracePieces !== null) applyMoveToPieces(tracePieces, parsedMoves[currentIndex]);
  currentIndex++;

  isAnimating = false;
  renderMoveDisplay();
  renderLog();
  renderRegisterView();
  updateButtonStates();
}

// Stepping backward is implemented by rebuilding a solved cube and replaying
// every prior move instantly (durationMs = 0), rather than animating true
// inverse moves. Simple, robust, and imperceptible for sequences this short.
async function stepPrev() {
  if (isAnimating || currentIndex === 0) return;
  const myGeneration = generation;
  isAnimating = true;
  updateButtonStates();

  const targetIndex = currentIndex - 1;
  rebuildCube();
  for (let i = 0; i < targetIndex; i++) {
    const { promise, cancel } = applyMove(scene, cubies, parsedMoves[i], 0);
    cancelCurrentMove = cancel;
    await promise;
    cancelCurrentMove = null;
    if (myGeneration !== generation) return;
  }
  currentIndex = targetIndex;

  if (tracePieces !== null) {
    tracePieces = createPieceState();
    for (let i = 0; i < currentIndex; i++) applyMoveToPieces(tracePieces, parsedMoves[i]);
  }

  isAnimating = false;
  renderMoveDisplay();
  renderLog();
  renderRegisterView();
  updateButtonStates();
}

async function skipToEnd() {
  if (isAnimating || currentIndex >= parsedMoves.length) return;
  const myGeneration = generation;
  isAnimating = true;
  pausePlayback();
  updateButtonStates();

  while (currentIndex < parsedMoves.length) {
    if (tracePieces !== null) applyMoveToPieces(tracePieces, parsedMoves[currentIndex]);
    const { promise, cancel } = applyMove(scene, cubies, parsedMoves[currentIndex], 0);
    cancelCurrentMove = cancel;
    await promise;
    cancelCurrentMove = null;
    if (myGeneration !== generation) return;
    currentIndex++;
  }

  isAnimating = false;
  renderMoveDisplay();
  renderLog();
  renderRegisterView();
  updateButtonStates();
}

function resetCube() {
  abortAnimation();
  pausePlayback();
  generation++;
  isAnimating = false;
  stopLineEmulator();
  currentIndex = 0;
  tracePieces = parsedMoves.length > 0 ? createPieceState() : null;
  rebuildCube();
  renderMoveDisplay();
  renderLog();
  renderRegisterView();
  updateButtonStates();
}

async function playLoop() {
  while (isPlaying && currentIndex < parsedMoves.length) {
    await stepNext();
  }
  isPlaying = false;
  playPauseBtn.innerHTML = '<span class="material-icons">play_arrow</span>';
  updateButtonStates();
}

function togglePlay() {
  if (parsedMoves.length === 0) return;

  if (isPlaying) {
    pausePlayback();
    return;
  }
  if (currentIndex >= parsedMoves.length) return;

  isPlaying = true;
  playPauseBtn.innerHTML = '<span class="material-icons">pause</span>';
  playLoop();
}

// ---------------------------------------------------------------------------
// Event wiring
// ---------------------------------------------------------------------------

runBtn.addEventListener('click', runProgram);
runSimplifiedBtn.addEventListener('click', runSimplifiedProgram);
runLineBtn.addEventListener('click', runLine);
nextBtn.addEventListener('click', stepNext);
skipBtn.addEventListener('click', skipToEnd);
prevBtn.addEventListener('click', stepPrev);
resetBtn.addEventListener('click', resetCube);
playPauseBtn.addEventListener('click', togglePlay);

modeSelect.addEventListener('change', () => {
  programError.textContent = '';
  showTextarea();
  if (modeSelect.value === 'cubyte') updateLineNumbers();
  updateButtonStates();
});

cubyteInput.addEventListener('input', updateLineNumbers);
cubyteInput.addEventListener('scroll', () => { cubyteLineNumbers.scrollTop = cubyteInput.scrollTop; });

updateLineNumbers();
updateButtonStates();

// ---------------------------------------------------------------------------
// Resize handles
// ---------------------------------------------------------------------------

function makeDraggable(handle, onDrag) {
  handle.addEventListener('mousedown', (e) => {
    e.preventDefault();
    handle.classList.add('dragging');
    document.body.style.userSelect = 'none';
    document.body.style.cursor = window.getComputedStyle(handle).cursor;

    function onMove(e) { onDrag(e); }
    function onUp() {
      handle.classList.remove('dragging');
      document.body.style.userSelect = '';
      document.body.style.cursor = '';
      document.removeEventListener('mousemove', onMove);
      document.removeEventListener('mouseup', onUp);
    }
    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
  });
}

makeDraggable(sidebarResizer, (e) => {
  const newWidth = Math.max(240, Math.min(900, window.innerWidth - e.clientX - 5));
  sidebarEl.style.width = newWidth + 'px';
  resizeRenderer();
});

makeDraggable(moveDisplayResizer, (e) => {
  const overlayTop = moveDisplay.parentElement.getBoundingClientRect().top;
  const newHeight = Math.max(24, Math.min(400, e.clientY - overlayTop - 14));
  moveDisplay.style.height = newHeight + 'px';
});

// ---------------------------------------------------------------------------
// Easter egg: type "jamie" while hovering the cube to toggle background image
// ---------------------------------------------------------------------------

const jamieBg = document.getElementById('jamie-bg');
const jamieScroll = jamieBg.querySelector('.jamie-scroll');
const sceneBgColor = scene.background;
let jamieActive = false;
let cursorInScene = false;
let keyBuffer = '';

function jamieScrollDuration() {
  const v = Number(speedSlider.value);
  const s = 10 - ((v - 30) / (1200 - 30)) * 9;
  return s.toFixed(2) + 's';
}

speedSlider.addEventListener('input', () => {
  if (jamieActive) jamieScroll.style.animationDuration = jamieScrollDuration();
});

container.addEventListener('mouseenter', () => { cursorInScene = true; });
container.addEventListener('mouseleave', () => { cursorInScene = false; keyBuffer = ''; });

document.addEventListener('keydown', (e) => {
  if (!cursorInScene || e.key.length !== 1) return;
  keyBuffer = (keyBuffer + e.key.toLowerCase()).slice(-5);
  if (keyBuffer !== 'jamie') return;
  keyBuffer = '';
  jamieActive = !jamieActive;
  scene.background = jamieActive ? null : sceneBgColor;
  jamieBg.style.display = jamieActive ? 'block' : 'none';
  if (jamieActive) jamieScroll.style.animationDuration = jamieScrollDuration();
});
