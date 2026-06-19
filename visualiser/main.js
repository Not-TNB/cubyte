import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { createCube, disposeCube, applyMove } from './cube.js';
import { parseAndBuildTrace, createInterpreter } from './program.js';

// ---------------------------------------------------------------------------
// DOM references
// ---------------------------------------------------------------------------

const container = document.getElementById('scene-container');
const programInput = document.getElementById('program-input');
const codeView = document.getElementById('code-view');
const runBtn = document.getElementById('run-btn');
const runLineBtn = document.getElementById('run-line-btn');
const programError = document.getElementById('program-error');
const programLog = document.getElementById('program-log');
const moveDisplay = document.getElementById('move-display');
const resetBtn = document.getElementById('reset-btn');
const prevBtn = document.getElementById('prev-btn');
const playPauseBtn = document.getElementById('play-pause-btn');
const nextBtn = document.getElementById('next-btn');
const speedSlider = document.getElementById('speed-slider');

// ---------------------------------------------------------------------------
// Scene / camera / renderer / controls
// ---------------------------------------------------------------------------

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x1b1d22);

const camera = new THREE.PerspectiveCamera(45, container.clientWidth / container.clientHeight, 0.1, 100);
camera.position.set(4.5, 4.5, 6);
camera.lookAt(0, 0, 0);

const renderer = new THREE.WebGLRenderer({ antialias: true });
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

window.addEventListener('resize', () => {
  camera.aspect = container.clientWidth / container.clientHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(container.clientWidth, container.clientHeight);
});

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
// Bumped by runProgram()/resetCube() so an in-flight stepNext/stepPrev that
// was started by a previous program run can detect it's been superseded and
// quietly abandon its post-animation bookkeeping instead of corrupting the
// freshly-reset state.
let generation = 0;

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

function updateButtonStates() {
  const hasMoves = parsedMoves.length > 0;
  const atEnd = currentIndex >= parsedMoves.length;
  prevBtn.disabled = !hasMoves || currentIndex === 0 || isAnimating;
  nextBtn.disabled = !hasMoves || atEnd || isAnimating;
  playPauseBtn.disabled = !hasMoves || (atEnd && !isPlaying);
  // Reset must stay clickable while the line emulator is active (it's the
  // only way back to editing) even though it never populates parsedMoves.
  resetBtn.disabled = !hasMoves && !lineInterpreter;
  runLineBtn.disabled = isAnimating || (lineInterpreter !== null && lineInterpreter.done);
}

function pausePlayback() {
  isPlaying = false;
  playPauseBtn.textContent = '▶';
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

function showTextarea() {
  programInput.hidden = false;
  codeView.hidden = true;
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
    const init = createInterpreter(programInput.value);
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

    lineInterpreter = init.interpreter;
    showCodeView(programInput.value);
    highlightRow(lineInterpreter.currentRow);
    updateButtonStates();
    return;
  }

  if (lineInterpreter.done) return;

  const myGeneration = generation;
  isAnimating = true;
  updateButtonStates();

  const durationMs = Number(speedSlider.value);
  const result = lineInterpreter.step();
  for (const move of result.moves) {
    await applyMove(scene, cubies, move, durationMs);
    if (myGeneration !== generation) return;
  }
  if (myGeneration !== generation) return;

  if (result.log) appendProgramLog(result.log);
  if (result.done) appendProgramLog('Program finished.');
  highlightRow(lineInterpreter.currentRow);

  isAnimating = false;
  updateButtonStates();
}

function runProgram() {
  programError.textContent = '';
  stopLineEmulator();

  const result = parseAndBuildTrace(programInput.value);
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
  rebuildCube();
  renderMoveDisplay();
  renderLog();
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

  const durationMs = Number(speedSlider.value);
  await applyMove(scene, cubies, parsedMoves[currentIndex], durationMs);
  // A Run/Reset click during the await already reset isAnimating/currentIndex
  // for the new program; don't stomp on that with this stale call's result.
  if (myGeneration !== generation) return;
  currentIndex++;

  isAnimating = false;
  renderMoveDisplay();
  renderLog();
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
    await applyMove(scene, cubies, parsedMoves[i], 0);
    if (myGeneration !== generation) return;
  }
  currentIndex = targetIndex;

  isAnimating = false;
  renderMoveDisplay();
  renderLog();
  updateButtonStates();
}

function resetCube() {
  pausePlayback();
  generation++;
  isAnimating = false;
  stopLineEmulator();
  currentIndex = 0;
  rebuildCube();
  renderMoveDisplay();
  renderLog();
  updateButtonStates();
}

async function playLoop() {
  while (isPlaying && currentIndex < parsedMoves.length) {
    await stepNext();
  }
  isPlaying = false;
  playPauseBtn.textContent = '▶';
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
  playPauseBtn.textContent = '⏸';
  playLoop();
}

// ---------------------------------------------------------------------------
// Event wiring
// ---------------------------------------------------------------------------

runBtn.addEventListener('click', runProgram);
runLineBtn.addEventListener('click', runLine);
nextBtn.addEventListener('click', stepNext);
prevBtn.addEventListener('click', stepPrev);
resetBtn.addEventListener('click', resetCube);
playPauseBtn.addEventListener('click', togglePlay);

updateButtonStates();
