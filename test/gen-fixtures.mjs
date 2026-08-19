/* Differential-test fixture generator.
 *
 * Runs Duranta's TypeScript polygon-clipping implementation (the reference)
 * over a corpus of fixed and pseudo-random inputs and records the results in
 * a flat text format that test_main.cpp replays through the C++ port.
 *
 * Requires Node >= 23.6 (TypeScript type stripping) and must be able to
 * resolve `splaytree` / `robust-predicates` from the TS file's location.
 *
 * Usage: node gen-fixtures.mjs [tsPath] [outPath]
 */

const tsPath =
  process.argv[2] ??
  "/Users/cyberax/duranta/app/frontend/packages/geo-utils/polygon-clipping.ts";
const outPath = process.argv[3] ?? new URL("./fixtures.txt", import.meta.url).pathname;

const { intersectionPacked, differencePacked, unionPacked, xor } = await import(tsPath);

import { writeFileSync } from "node:fs";

// ---- deterministic PRNG ----
function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// ---- shape helpers (all produce packed polygons) ----
const square = (x, y, size) => ({
  ringLengths: [5],
  packedCoordinates: [x, y, x + size, y, x + size, y + size, x, y + size, x, y],
});

const rect = (x, y, w, h) => ({
  ringLengths: [5],
  packedCoordinates: [x, y, x + w, y, x + w, y + h, x, y + h, x, y],
});

function squareWithHole(x, y, size, holeInset) {
  const hx = x + holeInset;
  const hy = y + holeInset;
  const hs = size - 2 * holeInset;
  return {
    ringLengths: [5, 5],
    packedCoordinates: [
      x, y, x + size, y, x + size, y + size, x, y + size, x, y,
      // hole, wound opposite (clockwise)
      hx, hy, hx, hy + hs, hx + hs, hy + hs, hx + hs, hy, hx, hy,
    ],
  };
}

function jitteredCircle(rand, cx, cy, radius, n, jitter) {
  const coords = [];
  for (let i = 0; i < n; i++) {
    const angle = (2 * Math.PI * i) / n;
    const r = radius * (1 + jitter * (rand() * 2 - 1));
    coords.push(cx + r * Math.cos(angle), cy + r * Math.sin(angle));
  }
  coords.push(coords[0], coords[1]);
  return { ringLengths: [n + 1], packedCoordinates: coords };
}

function star(rand, cx, cy, rOuter, rInner, points, jitter) {
  const coords = [];
  for (let i = 0; i < points * 2; i++) {
    const angle = (Math.PI * i) / points;
    const base = i % 2 === 0 ? rOuter : rInner;
    const r = base * (1 + jitter * (rand() * 2 - 1));
    coords.push(cx + r * Math.cos(angle), cy + r * Math.sin(angle));
  }
  coords.push(coords[0], coords[1]);
  return { ringLengths: [points * 2 + 1], packedCoordinates: coords };
}

// ---- fixture serialization ----
const lines = [];
const num = (v) => v.toString(); // shortest round-trip representation

function writeMultiPoly(tag, mp) {
  lines.push(`${tag} ${mp.length}`);
  for (const poly of mp) {
    lines.push(`POLY ${poly.ringLengths.length}`);
    let start = 0;
    for (const len of poly.ringLengths) {
      const coords = [];
      for (let p = start; p < start + len; p++) {
        coords.push(num(poly.packedCoordinates[p * 2]), num(poly.packedCoordinates[p * 2 + 1]));
      }
      lines.push(`RING ${len} ${coords.join(" ")}`);
      start += len;
    }
  }
}

let caseCount = 0;
function addCase(name, op, subject, clips) {
  caseCount++;
  lines.push(`CASE ${name.replace(/\s+/g, "_")}`);
  lines.push(`OP ${op}`);
  writeMultiPoly("SUBJECT", subject);
  lines.push(`CLIPS ${clips.length}`);
  for (const clip of clips) writeMultiPoly("CLIP", clip);
  let result = null;
  let error = null;
  try {
    if (op === "intersection") result = intersectionPacked(subject, ...clips);
    else if (op === "difference") result = differencePacked(subject, ...clips);
    else if (op === "union") result = unionPacked(subject, ...clips);
    else if (op === "xor") result = xorPacked(subject, clips);
    else throw new Error(`unknown op ${op}`);
  } catch (e) {
    error = e;
  }
  if (error !== null) {
    lines.push("EXPECT_ERROR");
  } else {
    writeMultiPoly("EXPECT", result);
  }
  lines.push("END");
}

// xor isn't exported in packed form; go through the nested-geometry API.
function packedToNested(mp) {
  return mp.map((poly) => {
    const rings = [];
    let start = 0;
    for (const len of poly.ringLengths) {
      const ring = [];
      for (let p = start; p < start + len; p++) {
        ring.push([poly.packedCoordinates[p * 2], poly.packedCoordinates[p * 2 + 1]]);
      }
      rings.push(ring);
      start += len;
    }
    return rings;
  });
}

function nestedToPacked(mpNested) {
  return mpNested.map((poly) => {
    const ringLengths = [];
    const packedCoordinates = [];
    for (const ring of poly) {
      ringLengths.push(ring.length);
      for (const pt of ring) packedCoordinates.push(pt[0], pt[1]);
    }
    return { ringLengths, packedCoordinates };
  });
}

function xorPacked(subject, clips) {
  const args = [packedToNested(subject), ...clips.map(packedToNested)];
  return nestedToPacked(xor(...args));
}

// ---- fixed cases ----
const ops = ["union", "intersection", "difference", "xor"];

for (const op of ops) {
  addCase(`basic_overlap_${op}`, op, [square(0, 0, 2)], [[square(1, 1, 2)]]);
  addCase(`disjoint_${op}`, op, [square(0, 0, 1)], [[square(5, 5, 1)]]);
  addCase(`contained_${op}`, op, [square(0, 0, 5)], [[square(1, 1, 1)]]);
  addCase(`identical_${op}`, op, [square(0, 0, 2)], [[square(0, 0, 2)]]);
  addCase(`shared_edge_${op}`, op, [square(0, 0, 1)], [[square(1, 0, 1)]]);
  addCase(`shared_corner_${op}`, op, [square(0, 0, 1)], [[square(1, 1, 1)]]);
  addCase(`partial_shared_edge_${op}`, op, [square(0, 0, 2)], [[rect(2, 0.5, 1, 1)]]);
  addCase(`hole_subject_${op}`, op, [squareWithHole(0, 0, 4, 1)], [[square(1.5, 1.5, 4)]]);
  addCase(`hole_both_${op}`, op, [squareWithHole(0, 0, 4, 1)], [[squareWithHole(2, 2, 4, 1)]]);
  addCase(`clip_inside_hole_${op}`, op, [squareWithHole(0, 0, 6, 2)], [[square(2.5, 2.5, 1)]]);
  addCase(
    `multipoly_subject_${op}`,
    op,
    [square(0, 0, 1), square(3, 0, 1), square(0, 3, 1)],
    [[rect(-1, 0.25, 6, 0.5)]],
  );
  addCase(
    `multiple_clips_${op}`,
    op,
    [square(0, 0, 4)],
    [[square(1, 1, 1)], [square(2.5, 2.5, 1)]],
  );
  addCase(`empty_subject_${op}`, op, [], [[square(0, 0, 1)]]);
  addCase(`no_clips_${op}`, op, [square(0, 0, 1)], []);
  addCase(
    `collinear_overlap_${op}`,
    op,
    [rect(0, 0, 4, 1)],
    [[rect(2, 0, 4, 1)]], // shares part of an edge line
  );
  addCase(
    `vertical_touch_${op}`,
    op,
    [
      {
        ringLengths: [4],
        packedCoordinates: [0, 0, 2, 0, 1, 2, 0, 0],
      },
    ],
    [[{ ringLengths: [4], packedCoordinates: [1, -1, 3, -1, 1, 1, 1, -1] }]],
  );
}

// Near-coincident vertices from the epsilon-snapping comment in the TS file.
addCase(
  "epsilon_snap_difference",
  "difference",
  [
    {
      ringLengths: [5],
      packedCoordinates: [
        -87.82174373924404, 42.104983, -87.8216, 42.104983, -87.8216, 42.10505, -87.82174373924447,
        42.10505, -87.82174373924404, 42.104983,
      ],
    },
  ],
  [[square(-87.8218, 42.1049, 0.0002)]],
);

// ---- random cases ----
const rand = mulberry32(0xdeadbeef);

// coordinate regimes: [name, cx, cy, scale]
const regimes = [
  ["unit", 0, 0, 1],
  ["geo", -87.8217, 42.105, 0.001],
  ["geo_large", -87.8, 42.1, 0.5],
  ["big", 1e6, -5e5, 1000],
];

for (const [rname, cx, cy, scale] of regimes) {
  for (let i = 0; i < 12; i++) {
    const op = ops[i % 4];
    const subj = [
      jitteredCircle(rand, cx + (rand() - 0.5) * scale, cy + (rand() - 0.5) * scale, scale * (0.2 + rand() * 0.5), 6 + Math.floor(rand() * 20), 0.3),
    ];
    const clip = [
      jitteredCircle(rand, cx + (rand() - 0.5) * scale, cy + (rand() - 0.5) * scale, scale * (0.2 + rand() * 0.5), 6 + Math.floor(rand() * 20), 0.3),
    ];
    addCase(`rand_circle_${rname}_${i}_${op}`, op, subj, [clip]);
  }
  for (let i = 0; i < 12; i++) {
    const op = ops[i % 4];
    const subj = [
      star(rand, cx, cy, scale * 0.6, scale * 0.25, 5 + Math.floor(rand() * 7), 0.2),
    ];
    const clip = [
      star(rand, cx + (rand() - 0.5) * scale * 0.5, cy + (rand() - 0.5) * scale * 0.5, scale * 0.6, scale * 0.25, 5 + Math.floor(rand() * 7), 0.2),
    ];
    addCase(`rand_star_${rname}_${i}_${op}`, op, subj, [clip]);
  }
  // multi-square mosaics: lots of shared/collinear edges
  for (let i = 0; i < 8; i++) {
    const op = ops[i % 4];
    const subj = [];
    const clip = [];
    for (let s = 0; s < 4; s++) {
      subj.push(
        square(cx + Math.floor(rand() * 5) * scale * 0.2, cy + Math.floor(rand() * 5) * scale * 0.2, scale * 0.2 * (1 + Math.floor(rand() * 2))),
      );
      clip.push(
        square(cx + Math.floor(rand() * 5) * scale * 0.2, cy + Math.floor(rand() * 5) * scale * 0.2, scale * 0.2 * (1 + Math.floor(rand() * 2))),
      );
    }
    addCase(`rand_mosaic_${rname}_${i}_${op}`, op, subj, [clip]);
  }
  // shapes with holes
  for (let i = 0; i < 8; i++) {
    const op = ops[i % 4];
    const subj = [squareWithHole(cx, cy, scale, scale * (0.1 + rand() * 0.3))];
    const clip = [
      squareWithHole(cx + (rand() - 0.5) * scale, cy + (rand() - 0.5) * scale, scale * (0.5 + rand()), scale * 0.1),
    ];
    addCase(`rand_holes_${rname}_${i}_${op}`, op, subj, [clip]);
  }
}

// near-degenerate: skinny slivers and nearly-parallel edges
for (let i = 0; i < 16; i++) {
  const op = ops[i % 4];
  const eps = Math.pow(10, -6 - (i % 8));
  const subj = [
    {
      ringLengths: [5],
      packedCoordinates: [0, 0, 10, eps, 10, 1, 0, 1 - eps, 0, 0],
    },
  ];
  const clip = [[{ ringLengths: [5], packedCoordinates: [1, -1, 9, -1 + eps * 2, 9, 2, 1, 2 - eps, 1, -1] }]];
  addCase(`sliver_${i}_${op}`, op, subj, clip);
}

// heavy shapes: high vertex counts
for (let i = 0; i < 8; i++) {
  const op = ops[i % 4];
  const n = 100 + Math.floor(rand() * 300);
  const subj = [jitteredCircle(rand, -87.8217, 42.105, 0.0004, n, 0.4)];
  const clip = [jitteredCircle(rand, -87.8216, 42.1049, 0.0004, n, 0.4)];
  addCase(`heavy_${i}_${op}`, op, subj, [clip]);
}

// chained ops: output of one op fed back as input (as the app's
// computePackedPolygonDifference does)
{
  let current = [jitteredCircle(rand, 0, 0, 10, 60, 0.3)];
  for (let i = 0; i < 10; i++) {
    const clip = [jitteredCircle(rand, (rand() - 0.5) * 10, (rand() - 0.5) * 10, 2 + rand() * 4, 20, 0.3)];
    const inter = intersectionPacked(current, clip);
    if (inter.length > 0) {
      addCase(`chain_inter_${i}`, "intersection", current, [clip]);
      let next;
      try {
        next = differencePacked(current, inter);
      } catch {
        break;
      }
      addCase(`chain_diff_${i}`, "difference", current, [inter]);
      if (next.length === 0) break;
      current = next;
    }
  }
}

writeFileSync(outPath, lines.join("\n") + "\n");
console.log(`Wrote ${caseCount} cases to ${outPath}`);
