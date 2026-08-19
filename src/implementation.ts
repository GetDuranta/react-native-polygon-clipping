/* Default (web / Node / Jest) implementation: the pure-TypeScript clipping
 * engine. React Native resolves implementation.native.ts instead, which
 * uses the C++ Turbo Module and falls back to this implementation when the
 * native module is unavailable. Both files must export the same API. */

import { union, intersection, xor, difference, intersectionPacked, differencePacked, unionPacked, xorPacked } from "./polygon-clipping";
import { splitEachJs } from "./split-each";
import type { DifferenceEachResult, SplitResult } from "./split-each";
import type { PackedCoords } from "./types";

export { union, intersection, xor, difference, intersectionPacked, differencePacked, unionPacked, xorPacked };

export function splitEachPacked(subjects: PackedCoords[], clips: PackedCoords[]): SplitResult[] {
  return splitEachJs(subjects, clips);
}

export function differenceEachPacked(subjects: PackedCoords[], clips: PackedCoords[]): DifferenceEachResult[] {
  return splitEachJs(subjects, clips).map(({ outside, touched, failures }) => ({
    outside,
    touched,
    failures,
  }));
}

export function isNativePolygonClippingAvailable(): boolean {
  return false;
}

export default { union, intersection, xor, difference };
