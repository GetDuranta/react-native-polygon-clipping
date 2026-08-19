// ---- Public API types ----

export type Pair = [number, number];
export type Ring = Pair[];
export type Polygon = Ring[];
export type MultiPolygon = Polygon[];
export type Geom = Polygon | MultiPolygon;

/* A polygon as flat coordinate storage: ringLengths[i] is the number of
 * coordinate pairs in ring i (first ring exterior, rest interior),
 * packedCoordinates holds x,y pairs for all rings back to back. */
export interface PackedCoords {
  id?: string;
  ringLengths: number[];
  packedCoordinates: number[];
}
