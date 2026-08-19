/* React Native implementation: routes through the C++ Turbo Module when it
 * is available and falls back to the TypeScript engine otherwise (Jest, old
 * architecture, remote debugging). Must export the same API as
 * implementation.ts. */

import type { Spec } from "./NativePolygonClipping";
import type { Geom, MultiPolygon, PackedCoords, Polygon, Ring } from "./types";
import * as js from "./polygon-clipping";
import { splitEachJs } from "./split-each";
import type { DifferenceEachResult, SplitResult } from "./split-each";

const OP_UNION = 0;
const OP_INTERSECTION = 1;
const OP_XOR = 2;
const OP_DIFFERENCE = 3;

// undefined = not probed yet, null = probed and unavailable
let nativeModule: Spec | null | undefined;

function native(): Spec | null {
  if (nativeModule === undefined) {
    try {
      // Lazy so merely importing this package never throws.
      // eslint-disable-next-line @typescript-eslint/no-var-requires
      nativeModule = (require("./NativePolygonClipping") as { default: Spec }).default;
    } catch {
      nativeModule = null;
    }
  }
  return nativeModule;
}

export function isNativePolygonClippingAvailable(): boolean {
  return native() !== null;
}

// ---- flat geometry encoding (see NativePolygonClipping.ts) ----

function appendMultiPoly(out: number[], mp: PackedCoords[]): void {
  out.push(mp.length);
  for (const poly of mp) {
    const { ringLengths, packedCoordinates } = poly;
    out.push(ringLengths.length);
    let coordIndex = 0;
    for (let r = 0; r < ringLengths.length; r++) {
      const pairs = ringLengths[r];
      out.push(pairs);
      for (let i = 0; i < pairs * 2; i++) {
        out.push(packedCoordinates[coordIndex++]);
      }
    }
  }
}

function encodeGeometry(multiPolys: PackedCoords[][]): number[] {
  const out: number[] = [multiPolys.length];
  for (const mp of multiPolys) appendMultiPoly(out, mp);
  return out;
}

function encodeMultiPoly(mp: PackedCoords[]): number[] {
  const out: number[] = [];
  appendMultiPoly(out, mp);
  return out;
}

/* Decodes one multipolygon starting at `cursor.pos`, advancing the cursor. */
function decodeMultiPoly(data: number[], cursor: { pos: number }): PackedCoords[] {
  const result: PackedCoords[] = [];
  const numPolys = data[cursor.pos++];
  for (let p = 0; p < numPolys; p++) {
    const numRings = data[cursor.pos++];
    const ringLengths: number[] = [];
    const packedCoordinates: number[] = [];
    for (let ring = 0; ring < numRings; ring++) {
      const pairs = data[cursor.pos++];
      ringLengths.push(pairs);
      for (let i = 0; i < pairs * 2; i++) {
        packedCoordinates.push(data[cursor.pos++]);
      }
    }
    result.push({ ringLengths, packedCoordinates });
  }
  return result;
}

function clipPacked(module: Spec, op: number, subject: PackedCoords[], clips: PackedCoords[][]): PackedCoords[] {
  const data = module.clip(op, encodeGeometry([subject, ...clips]));
  return decodeMultiPoly(data, { pos: 0 });
}

// ---- packed API ----

export function unionPacked(subject: PackedCoords[], ...more: PackedCoords[][]): PackedCoords[] {
  const module = native();
  if (module === null) return js.unionPacked(subject, ...more);
  return clipPacked(module, OP_UNION, subject, more);
}

export function intersectionPacked(subject: PackedCoords[], ...clips: PackedCoords[][]): PackedCoords[] {
  const module = native();
  if (module === null) return js.intersectionPacked(subject, ...clips);
  return clipPacked(module, OP_INTERSECTION, subject, clips);
}

export function xorPacked(subject: PackedCoords[], ...more: PackedCoords[][]): PackedCoords[] {
  const module = native();
  if (module === null) return js.xorPacked(subject, ...more);
  return clipPacked(module, OP_XOR, subject, more);
}

export function differencePacked(subject: PackedCoords[], ...clips: PackedCoords[][]): PackedCoords[] {
  const module = native();
  if (module === null) return js.differencePacked(subject, ...clips);
  return clipPacked(module, OP_DIFFERENCE, subject, clips);
}

// ---- bulk API ----

export function splitEachPacked(subjects: PackedCoords[], clips: PackedCoords[]): SplitResult[] {
  const module = native();
  if (module === null) return splitEachJs(subjects, clips);
  const data = module.splitEach(encodeMultiPoly(subjects), encodeMultiPoly(clips));
  const cursor = { pos: 0 };
  const numSubjects = data[cursor.pos++];
  const results: SplitResult[] = [];
  for (let i = 0; i < numSubjects; i++) {
    const touched = data[cursor.pos++] !== 0;
    const failures = data[cursor.pos++];
    const outside = decodeMultiPoly(data, cursor);
    const inside = decodeMultiPoly(data, cursor);
    results.push({ outside, inside, touched, failures });
  }
  return results;
}

export function differenceEachPacked(subjects: PackedCoords[], clips: PackedCoords[]): DifferenceEachResult[] {
  return splitEachPacked(subjects, clips).map(({ outside, touched, failures }) => ({
    outside,
    touched,
    failures,
  }));
}

// ---- nested-geometry API ----

function isMultiPolygon(geom: Geom): geom is MultiPolygon {
  // Same duck-typing as polygon-clipping: a Polygon is Ring[] whose first
  // ring's first element is a coordinate pair.
  try {
    return typeof (geom as MultiPolygon)[0][0][0] !== "number";
  } catch {
    return false;
  }
}

function geomToPacked(geom: Geom): PackedCoords[] {
  const multiPoly: MultiPolygon = isMultiPolygon(geom) ? geom : [geom as Polygon];
  return multiPoly.map((poly) => {
    const ringLengths: number[] = [];
    const packedCoordinates: number[] = [];
    for (const ring of poly) {
      ringLengths.push(ring.length);
      for (const pt of ring) {
        packedCoordinates.push(pt[0], pt[1]);
      }
    }
    return { ringLengths, packedCoordinates };
  });
}

function packedToMultiPolygon(packed: PackedCoords[]): MultiPolygon {
  return packed.map((poly) => {
    const rings: Ring[] = [];
    let start = 0;
    for (const pairs of poly.ringLengths) {
      const ring: Ring = [];
      for (let p = start; p < start + pairs; p++) {
        ring.push([poly.packedCoordinates[p * 2], poly.packedCoordinates[p * 2 + 1]]);
      }
      rings.push(ring);
      start += pairs;
    }
    return rings;
  });
}

function clipGeom(module: Spec, op: number, subject: Geom, others: Geom[]): MultiPolygon {
  return packedToMultiPolygon(
    clipPacked(module, op, geomToPacked(subject), others.map(geomToPacked)),
  );
}

export function union(geom: Geom, ...moreGeoms: Geom[]): MultiPolygon {
  const module = native();
  if (module === null) return js.union(geom, ...moreGeoms);
  return clipGeom(module, OP_UNION, geom, moreGeoms);
}

export function intersection(geom: Geom, ...moreGeoms: Geom[]): MultiPolygon {
  const module = native();
  if (module === null) return js.intersection(geom, ...moreGeoms);
  return clipGeom(module, OP_INTERSECTION, geom, moreGeoms);
}

export function xor(geom: Geom, ...moreGeoms: Geom[]): MultiPolygon {
  const module = native();
  if (module === null) return js.xor(geom, ...moreGeoms);
  return clipGeom(module, OP_XOR, geom, moreGeoms);
}

export function difference(subjectGeom: Geom, ...clippingGeoms: Geom[]): MultiPolygon {
  const module = native();
  if (module === null) return js.difference(subjectGeom, ...clippingGeoms);
  return clipGeom(module, OP_DIFFERENCE, subjectGeom, clippingGeoms);
}

export default { union, intersection, xor, difference };
