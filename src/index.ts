/* Platform dispatch happens at the './implementation' import: Metro (and
 * jest-expo) resolve implementation.native.ts, which uses the C++ Turbo
 * Module with a TypeScript fallback; web bundlers and Node resolve
 * implementation.ts, the pure-TypeScript engine. Neither path imports
 * 'react-native' outside of React Native. */

export type { Pair, Ring, Polygon, MultiPolygon, Geom, PackedCoords } from "./types";
export type { SplitResult, DifferenceEachResult, SplitMergedResult } from "./split-each";

export {
  union,
  intersection,
  xor,
  difference,
  unionPacked,
  intersectionPacked,
  xorPacked,
  differencePacked,
  splitEachPacked,
  differenceEachPacked,
  splitMergedPacked,
  isNativePolygonClippingAvailable,
  default,
} from "./implementation";
