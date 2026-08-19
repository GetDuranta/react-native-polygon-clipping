#pragma once

#include <cstdint>
#include <vector>

/* C++ translation of the Martinez-Rueda-Feito polygon clipping algorithm.
 *
 * Derived from the `polygon-clipping` npm package by Mike Fogel (MIT
 * license, https://github.com/mfogel/polygon-clipping), via Duranta's
 * TypeScript fork (epsilon snapping at 1e-12, packed-coordinate I/O,
 * iteration guards), with the robust point-vs-segment orientation test
 * from `polyclip-ts` (https://github.com/luizbarboza/polyclip-ts). */

namespace polyclip {

enum class OpType { Union, Intersection, Xor, Difference };

/* A polygon as flat coordinate storage: ringLengths[i] is the number of
 * coordinate pairs in ring i (first ring exterior, rest interior), coords
 * holds x,y pairs for all rings back to back. Rings may be open or closed
 * (a closing point equal to the first is optional on input; output rings
 * are closed, matching the TypeScript implementation). */
struct PackedPolygon {
  std::vector<int32_t> ringLengths;
  std::vector<double> coords;
};

using PackedMultiPolygon = std::vector<PackedPolygon>;

struct ClipOptions {
  /* EXPERIMENTAL: use the exact orient2d predicate for point-vs-segment
   * comparison (polyclip-ts behavior) instead of the original
   * polygon-clipping division-based test.
   *
   * Off by default: the default mode is bit-exact with the TypeScript
   * implementation across the differential corpus, while the exact
   * predicate combined with the coarse 1e-12 coordinate snapping was
   * observed to *cause* "Unable to complete output ring" failures on
   * chained operations (cf. polyclip-ts issue #23: exact collinearity
   * decisions disagree with epsilon-snapped geometry). */
  bool robustComparePoint = false;
  /* Guards against infinite loops caused by floating-point round-off. */
  int64_t maxQueueSize = 1000000;
  int64_t maxSweepLineSegments = 1000000;
};

/* Performs `op` on subject vs clips. Throws std::invalid_argument on
 * malformed input and std::runtime_error on topology failures (same
 * conditions that throw in the TypeScript implementation). */
PackedMultiPolygon clip(OpType op, const PackedMultiPolygon& subject,
                        const std::vector<PackedMultiPolygon>& clips,
                        const ClipOptions& options = {});

} // namespace polyclip
