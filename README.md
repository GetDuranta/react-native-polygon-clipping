# @duranta-public/react-native-polygon-clipping

Fast boolean operations on polygons — union, intersection, difference, xor —
for React Native, implemented as a **pure C++ Turbo Module** (no Java/Kotlin,
no Swift/ObjC wrappers around the algorithm, synchronous JSI calls), with a
**TypeScript fallback** that runs the same algorithm wherever the native
module isn't available: web bundles, Node, Jest, the old architecture. The
package can therefore be used as the single polygon-clipping dependency of a
codebase that targets both React Native and the web.

The clipping core is a line-faithful C++ translation of the
[Martinez-Rueda-Feito](https://doi.org/10.1016/j.advengsoft.2013.04.004)
sweep-line algorithm from the
[`polygon-clipping`](https://github.com/mfogel/polygon-clipping) library
(MIT), with:

- epsilon snapping (1e-12, relative) of near-coincident coordinates, which
  prevents the sweep-line failures that floating-point noise causes on
  real-world geographic inputs;
- the robust [`robust-predicates`](https://github.com/mourner/robust-predicates)
  `orient2d` for collinearity decisions (exact-precision, ISC);
- iteration guards against the infinite loops that round-off errors can
  otherwise cause;
- iterative (not recursive) state propagation, safe for mobile thread stacks.

The C++ port is **bit-exact** with its TypeScript reference across a
differential corpus of 265 fixed, random, degenerate, and chained-operation
cases (see *Testing* below). In a Node/V8 comparison on identical inputs it
is ~1.7× faster than the JIT-compiled TypeScript; on Hermes, where JS runs
interpreted, expect one to two orders of magnitude.

## Requirements

- For the native module: React Native **>= 0.76** with the New Architecture
  enabled (pure C++ Turbo Module autolinking). Works with Expo SDK 52+
  (expo-modules-autolinking supports pure C++ dependencies).
- Everywhere else (web, Node >= 18, Jest) the TypeScript implementation is
  used automatically; no React Native required in that dependency graph.

## Installation

```sh
npm install @duranta-public/react-native-polygon-clipping
cd ios && pod install
```

No other setup: the module autolinks on both platforms (Android via the
`cxxModule*` autolinking config, iOS via `registerCxxModuleToGlobalModuleMap`).
Codegen artifacts are shipped in the package (`includesGeneratedCode: true`),
so no extra codegen step runs in your app.

## Usage

GeoJSON-style nested geometry (drop-in for the `polygon-clipping` package):

```ts
import { union, intersection, difference, xor } from "@duranta-public/react-native-polygon-clipping";

const subject = [[[0, 0], [2, 0], [2, 2], [0, 2], [0, 0]]];   // Polygon
const clip = [[[1, 1], [3, 1], [3, 3], [1, 3], [1, 1]]];

const result = intersection(subject, clip);                    // MultiPolygon
```

Packed flat-coordinate geometry (faster — no per-vertex array allocation):

```ts
import {
  intersectionPacked,
  differencePacked,
  unionPacked,
  xorPacked,
  type PackedCoords,
} from "@duranta-public/react-native-polygon-clipping";

const square: PackedCoords = {
  ringLengths: [5], // coordinate pairs per ring; first ring is the exterior
  packedCoordinates: [0, 0, 2, 0, 2, 2, 0, 2, 0, 0],
};

const result: PackedCoords[] = differencePacked([square], [clip]);
```

Bulk splitting — clips many subject polygons against a set of clip polygons
in a single native call (one JS↔native crossing instead of two per
subject×clip pair):

```ts
import { splitEachPacked, differenceEachPacked } from "@duranta-public/react-native-polygon-clipping";

// Per subject polygon, iterates the clips in order: intersects the current
// remainder with the clip and, when non-empty, subtracts the intersection.
const results = splitEachPacked(subjects, clips);
// results[i] = {
//   outside:  PackedCoords[]  pieces of subjects[i] outside all clips
//                             (verbatim copy of the input when untouched),
//   inside:   PackedCoords[]  intersection pieces carved out, in clip order,
//   touched:  boolean         false = no clip modified this subject,
//   failures: number          clip steps skipped due to topology errors
//                             (their intersections remain in `inside`),
// }

// Same loop without collecting the inside pieces in the result:
const diffs = differenceEachPacked(subjects, clips); // { outside, touched, failures }[]
```

Intersection failures propagate as exceptions; difference failures are
counted per subject and that clip step is skipped, so one degenerate
geometry doesn't discard the rest of the batch.

Notes:

- Input rings may be open or closed; output rings are always closed
  (first point repeated at the end). Exterior rings are counter-clockwise,
  interior rings clockwise.
- All functions are synchronous and safe to call from render/gesture code.
- On invalid input or internal topology failure the functions throw an
  `Error` with the same messages as `polygon-clipping` (e.g.
  `"Unable to complete output ring…"`), so existing error handling keeps
  working.
- `isNativePolygonClippingAvailable()` reports which implementation is in
  use (false means the TypeScript fallback).

## Architecture

```
src/index.ts                   public API
src/implementation.ts          web/Node entry: TypeScript engine
src/implementation.native.ts   React Native entry: Turbo Module, falling
                               back to the TypeScript engine when absent
src/polygon-clipping.ts        the TypeScript engine (reference for the C++)
src/NativePolygonClipping.ts   Turbo Module spec (codegen input)
cpp/PolygonClippingImpl.*      the Turbo Module (decode → clip → encode)
cpp/core/polyclip.*            the clipping algorithm (RN-independent)
cpp/core/orient2d.*            robust orientation predicate
android/CMakeLists.txt         builds the C++ into the app (autolinked)
android/generated, ios/generated  shipped codegen artifacts
ios/OnLoad.mm                  registers the module on iOS
```

Platform selection uses the standard `.native.ts` convention: Metro (and
jest-expo) resolve `implementation.native.ts`; web bundlers and Node resolve
`implementation.ts`, whose import graph never touches `react-native`.
Geometry crosses the JSI boundary as a single flat double array per call.

The core (`cpp/core`) has no React Native dependencies and builds with any
C++17 compiler; the algorithm requires **FP contraction disabled**
(`-ffp-contract=off`, already set in the Android CMake and the podspec) to
keep exact IEEE-754 double semantics.

`ClipOptions::robustComparePoint` selects an experimental variant that uses
exact `orient2d` for point-vs-segment tests (as
[polyclip-ts](https://github.com/luizbarboza/polyclip-ts) does). It is off
by default: differential testing showed the exact predicate can conflict
with the epsilon-snapped geometry on chained operations and cause
"Unable to complete output ring" failures the default mode doesn't have.

## Testing

`test/run-tests.sh` builds and runs host-side tests: built-in unit tests
plus a differential harness that replays fixtures generated from the bundled
TypeScript implementation (`test/gen-fixtures.mjs`, requires `npm install`
and Node >= 23.6 for TypeScript type stripping) and requires bit-exact
output from the C++ port.
`test/bench.mjs` and `polyclip_test fixtures.txt bench` run the same corpus
through both implementations for timing.

## Regenerating codegen artifacts

After changing `src/NativePolygonClipping.ts`:

```sh
npx react-native codegen   # or: yarn prepare (react-native-builder-bob)
```

## License

BSD-1-Clause. Contains code derived from `polygon-clipping` (MIT) and
`robust-predicates` (ISC) — see [LICENSE](LICENSE) for third-party notices.
