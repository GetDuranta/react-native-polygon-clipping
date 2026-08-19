import type { Spec } from "./NativePolygonClipping";

// ---- Public API types (mirroring polygon-clipping / Duranta's poly-clip) ----

export type Pair = [number, number];
export type Ring = Pair[];
export type Polygon = Ring[];
export type MultiPolygon = Polygon[];
export type Geom = Polygon | MultiPolygon;

export interface PackedCoords {
  id?: string;
  ringLengths: number[];
  packedCoordinates: number[];
}

const OP_UNION = 0;
const OP_INTERSECTION = 1;
const OP_XOR = 2;
const OP_DIFFERENCE = 3;

// Lazy so importing this module (e.g. for types) doesn't throw in
// environments without the native module (web, jest).
let nativeModule: Spec | null = null;
function native(): Spec {
  if (nativeModule === null) {
    // eslint-disable-next-line @typescript-eslint/no-var-requires
    nativeModule = (require("./NativePolygonClipping") as { default: Spec }).default;
  }
  return nativeModule;
}

export function isNativePolygonClippingAvailable(): boolean {
  try {
    native();
    return true;
  } catch {
    return false;
  }
}

// ---- flat geometry encoding (see NativePolygonClipping.ts) ----

function encodePacked(multiPolys: PackedCoords[][]): number[] {
  let size = 1;
  for (const mp of multiPolys) {
    size += 1;
    for (const poly of mp) {
      size += 1 + poly.ringLengths.length + poly.packedCoordinates.length;
    }
  }
  const out = new Array<number>(size);
  let w = 0;
  out[w++] = multiPolys.length;
  for (const mp of multiPolys) {
    out[w++] = mp.length;
    for (const poly of mp) {
      const { ringLengths, packedCoordinates } = poly;
      out[w++] = ringLengths.length;
      let coordIndex = 0;
      for (let r = 0; r < ringLengths.length; r++) {
        const pairs = ringLengths[r];
        out[w++] = pairs;
        for (let i = 0; i < pairs * 2; i++) {
          out[w++] = packedCoordinates[coordIndex++];
        }
      }
    }
  }
  return out;
}

function decodePacked(data: number[]): PackedCoords[] {
  const result: PackedCoords[] = [];
  let r = 0;
  const numPolys = data[r++];
  for (let p = 0; p < numPolys; p++) {
    const numRings = data[r++];
    const ringLengths: number[] = [];
    const packedCoordinates: number[] = [];
    for (let ring = 0; ring < numRings; ring++) {
      const pairs = data[r++];
      ringLengths.push(pairs);
      for (let i = 0; i < pairs * 2; i++) {
        packedCoordinates.push(data[r++]);
      }
    }
    result.push({ ringLengths, packedCoordinates });
  }
  return result;
}

function clipPacked(op: number, subject: PackedCoords[], clips: PackedCoords[][]): PackedCoords[] {
  const encoded = encodePacked([subject, ...clips]);
  return decodePacked(native().clip(op, encoded));
}

// ---- packed API (drop-in for Duranta's poly-clip packed functions) ----

export function unionPacked(subject: PackedCoords[], ...more: PackedCoords[][]): PackedCoords[] {
  return clipPacked(OP_UNION, subject, more);
}

export function intersectionPacked(subject: PackedCoords[], ...clips: PackedCoords[][]): PackedCoords[] {
  return clipPacked(OP_INTERSECTION, subject, clips);
}

export function xorPacked(subject: PackedCoords[], ...more: PackedCoords[][]): PackedCoords[] {
  return clipPacked(OP_XOR, subject, more);
}

export function differencePacked(subject: PackedCoords[], ...clips: PackedCoords[][]): PackedCoords[] {
  return clipPacked(OP_DIFFERENCE, subject, clips);
}

// ---- nested-geometry API (drop-in for the polygon-clipping package) ----

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

function clipGeom(op: number, subject: Geom, others: Geom[]): MultiPolygon {
  return packedToMultiPolygon(clipPacked(op, geomToPacked(subject), others.map(geomToPacked)));
}

export function union(geom: Geom, ...moreGeoms: Geom[]): MultiPolygon {
  return clipGeom(OP_UNION, geom, moreGeoms);
}

export function intersection(geom: Geom, ...moreGeoms: Geom[]): MultiPolygon {
  return clipGeom(OP_INTERSECTION, geom, moreGeoms);
}

export function xor(geom: Geom, ...moreGeoms: Geom[]): MultiPolygon {
  return clipGeom(OP_XOR, geom, moreGeoms);
}

export function difference(subjectGeom: Geom, ...clippingGeoms: Geom[]): MultiPolygon {
  return clipGeom(OP_DIFFERENCE, subjectGeom, clippingGeoms);
}

export default { union, intersection, xor, difference };
