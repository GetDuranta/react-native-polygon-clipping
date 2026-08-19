/* Default (web / Node / Jest) implementation: the pure-TypeScript clipping
 * engine. React Native resolves implementation.native.ts instead, which
 * uses the C++ Turbo Module and falls back to this implementation when the
 * native module is unavailable. Both files must export the same API. */

import { union, intersection, xor, difference, intersectionPacked, differencePacked, unionPacked, xorPacked } from "./polygon-clipping";

export { union, intersection, xor, difference, intersectionPacked, differencePacked, unionPacked, xorPacked };

export function isNativePolygonClippingAvailable(): boolean {
  return false;
}

export default { union, intersection, xor, difference };
