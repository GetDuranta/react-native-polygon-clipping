/* Replays every non-error fixture from fixtures.txt through the TypeScript
 * reference implementation, mirroring `polyclip_test fixtures.txt bench N`.
 *
 * Usage: node bench.mjs [tsPath] [fixturesPath] [iters]
 */
import { readFileSync } from "node:fs";

const tsPath =
  process.argv[2] ??
  new URL("../src/polygon-clipping.ts", import.meta.url).pathname;
const fixturesPath = process.argv[3] ?? new URL("./fixtures.txt", import.meta.url).pathname;
const iters = Number(process.argv[4] ?? 20);

const { intersectionPacked, differencePacked, unionPacked, xor } = await import(tsPath);

const tokens = readFileSync(fixturesPath, "utf8").split(/\s+/).filter(Boolean);
let pos = 0;
const next = () => tokens[pos++];
const nextNum = () => Number(next());

function readMultiPoly(numPolys) {
  const mp = [];
  for (let p = 0; p < numPolys; p++) {
    if (next() !== "POLY") throw new Error("expected POLY");
    const numRings = nextNum();
    const ringLengths = [];
    const packedCoordinates = [];
    for (let r = 0; r < numRings; r++) {
      if (next() !== "RING") throw new Error("expected RING");
      const numPairs = nextNum();
      ringLengths.push(numPairs);
      for (let i = 0; i < numPairs * 2; i++) packedCoordinates.push(nextNum());
    }
    mp.push({ ringLengths, packedCoordinates });
  }
  return mp;
}

const cases = [];
while (pos < tokens.length) {
  if (next() !== "CASE") throw new Error("expected CASE");
  const name = next();
  next(); // OP
  const op = next();
  next(); // SUBJECT
  const subject = readMultiPoly(nextNum());
  next(); // CLIPS
  const numClips = nextNum();
  const clips = [];
  for (let c = 0; c < numClips; c++) {
    next(); // CLIP
    clips.push(readMultiPoly(nextNum()));
  }
  const expectTag = next();
  let expectError = expectTag === "EXPECT_ERROR";
  if (!expectError) readMultiPoly(nextNum());
  if (next() !== "END") throw new Error("expected END");
  if (!expectError) cases.push({ name, op, subject, clips });
}

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

function run(c) {
  if (c.op === "intersection") return intersectionPacked(c.subject, ...c.clips);
  if (c.op === "difference") return differencePacked(c.subject, ...c.clips);
  if (c.op === "union") return unionPacked(c.subject, ...c.clips);
  return xor(packedToNested(c.subject), ...c.clips.map(packedToNested));
}

// warmup (JIT)
for (const c of cases) run(c);

let sink = 0;
let ops = 0;
const start = performance.now();
for (let it = 0; it < iters; it++) {
  for (const c of cases) {
    sink += run(c).length;
    ops++;
  }
}
const ms = performance.now() - start;
console.log(`bench: ${ops} ops in ${ms.toFixed(1)} ms (${(ms / ops).toFixed(3)} ms/op) [sink ${sink}]`);
