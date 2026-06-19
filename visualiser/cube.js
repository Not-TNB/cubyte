import * as THREE from 'three';
import { AXIS, LAYER, angleDegFor, stepsFor, rotateVectorStep } from './cubeMath.js';

// ---------------------------------------------------------------------------
// Geometry constants
// ---------------------------------------------------------------------------

const CUBIE_SIZE = 0.92;
const SPACING = 1;

const COLORS = {
  U: 0xffffff, // white
  D: 0xffd500, // yellow
  F: 0x009e60, // green
  B: 0x0051ba, // blue
  R: 0xc41e3a, // red
  L: 0xff5800, // orange
  INNER: 0x1a1a1a,
};

const GRID = [-1, 0, 1];

const materialCache = new Map();

function materialFor(colorHex) {
  if (!materialCache.has(colorHex)) {
    materialCache.set(
      colorHex,
      new THREE.MeshStandardMaterial({ color: colorHex, roughness: 0.4, metalness: 0.05 })
    );
  }
  return materialCache.get(colorHex);
}

// BoxGeometry material array order is [+x, -x, +y, -y, +z, -z].
function buildMaterials(x, y, z) {
  return [
    materialFor(x === 1 ? COLORS.R : COLORS.INNER),
    materialFor(x === -1 ? COLORS.L : COLORS.INNER),
    materialFor(y === 1 ? COLORS.U : COLORS.INNER),
    materialFor(y === -1 ? COLORS.D : COLORS.INNER),
    materialFor(z === 1 ? COLORS.F : COLORS.INNER),
    materialFor(z === -1 ? COLORS.B : COLORS.INNER),
  ];
}

// Builds a fresh solved 3x3x3 cube and adds it to the scene.
// Returns an array of 27 cubies: { mesh, x, y, z } where x,y,z are the
// logical grid coordinates (each in {-1,0,1}), kept in sync with the mesh's
// world position/orientation after every move.
function createCube(scene) {
  const cubies = [];
  const geometry = new THREE.BoxGeometry(CUBIE_SIZE, CUBIE_SIZE, CUBIE_SIZE);

  for (const x of GRID) {
    for (const y of GRID) {
      for (const z of GRID) {
        const mesh = new THREE.Mesh(geometry, buildMaterials(x, y, z));
        mesh.position.set(x * SPACING, y * SPACING, z * SPACING);
        scene.add(mesh);
        cubies.push({ mesh, x, y, z });
      }
    }
  }

  return cubies;
}

// Removes a cube's meshes from the scene. Materials are cached/shared at
// module scope and intentionally not disposed; geometry is shared too (one
// BoxGeometry instance reused by all 27 cubies), so there is nothing per-mesh
// to dispose beyond detaching it from the scene graph.
function disposeCube(scene, cubies) {
  for (const cubie of cubies) {
    scene.remove(cubie.mesh);
  }
}

function getLayerCubies(cubies, face) {
  const [axisKey, value] = LAYER[face];
  return cubies.filter((c) => c[axisKey] === value);
}

function easeInOutQuad(t) {
  return t < 0.5 ? 2 * t * t : 1 - ((-2 * t + 2) ** 2) / 2;
}

// Resolves once the pivot's rotation has been driven from 0 to targetRad
// over durationMs (or instantly, if durationMs <= 0).
function animateRotation(pivot, axis, targetRad, durationMs) {
  if (durationMs <= 0) {
    pivot.rotation[axis] = targetRad;
    return Promise.resolve();
  }

  return new Promise((resolve) => {
    const start = performance.now();

    function step(now) {
      const elapsed = now - start;
      const t = Math.min(elapsed / durationMs, 1);
      pivot.rotation[axis] = targetRad * easeInOutQuad(t);

      if (t < 1) {
        requestAnimationFrame(step);
      } else {
        pivot.rotation[axis] = targetRad;
        resolve();
      }
    }

    requestAnimationFrame(step);
  });
}

// Rounds a mesh's orientation to the nearest exact 90deg-aligned rotation,
// eliminating floating-point drift accumulated across repeated turns. Valid
// because every orientation this cube can reach is one of the 24 rotations
// of a cube, whose rotation-matrix entries are always exactly -1, 0, or 1.
function snapMeshOrientation(mesh) {
  const matrix = new THREE.Matrix4().makeRotationFromQuaternion(mesh.quaternion);
  const e = matrix.elements;
  for (let i = 0; i < e.length; i++) {
    e[i] = Math.round(e[i]);
  }
  mesh.quaternion.setFromRotationMatrix(matrix);
}

// Applies one move to the cube: re-parents the affected layer's 9 cubies
// under a temporary pivot, rotates the pivot (animated unless durationMs is
// 0), re-parents back to the scene, then updates each cubie's logical grid
// coordinates and snaps both position and orientation to remove drift.
async function applyMove(scene, cubies, move, durationMs) {
  const { face } = move;
  const axis = AXIS[face];
  const angleDeg = angleDegFor(move);
  const targetRad = THREE.MathUtils.degToRad(angleDeg);
  const layerCubies = getLayerCubies(cubies, face);

  const pivot = new THREE.Group();
  scene.add(pivot);
  for (const cubie of layerCubies) {
    pivot.attach(cubie.mesh);
  }

  await animateRotation(pivot, axis, targetRad, durationMs);

  for (const cubie of layerCubies) {
    scene.attach(cubie.mesh);
  }
  scene.remove(pivot);

  const steps = stepsFor(move);
  for (const cubie of layerCubies) {
    for (let i = 0; i < steps; i++) {
      rotateVectorStep(cubie, axis);
    }
    cubie.mesh.position.set(cubie.x * SPACING, cubie.y * SPACING, cubie.z * SPACING);
    snapMeshOrientation(cubie.mesh);
  }
}

export { createCube, disposeCube, applyMove, getLayerCubies };
