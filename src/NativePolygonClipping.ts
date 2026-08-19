import type { TurboModule } from "react-native";
import { TurboModuleRegistry } from "react-native";

export interface Spec extends TurboModule {
  /**
   * Performs a boolean operation on multipolygons.
   *
   * op: 0 = union, 1 = intersection, 2 = xor, 3 = difference.
   *
   * `geometry` is a flat double array encoding the subject followed by the
   * clipping multipolygons:
   *
   *   geometry   := numMultiPolys multiPoly*   (first multiPoly is the subject)
   *   multiPoly  := numPolys poly*
   *   poly       := numRings ring*
   *   ring       := numPairs (x y)*
   *
   * The return value is a single multipolygon encoded as `multiPoly` above
   * (without the numMultiPolys prefix). Throws on invalid input geometry or
   * on internal topology errors, with the same messages as the original
   * polygon-clipping library.
   */
  readonly clip: (op: number, geometry: ReadonlyArray<number>) => number[];
}

export default TurboModuleRegistry.getEnforcing<Spec>("PolygonClipping");
