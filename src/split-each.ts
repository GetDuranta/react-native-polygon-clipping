/* JavaScript implementation of the splitEach bulk operation, mirroring
 * cpp/core/polyclip.cpp splitEach(). Used directly on web/Node and as the
 * fallback on React Native. Any behavioral change here must be mirrored in
 * the C++ implementation. */

import { differencePacked, intersectionPacked } from "./polygon-clipping";
import type { PackedCoords } from "./types";

export interface SplitResult {
  /* Pieces of the subject that lie outside all clips. When touched is
   * false this is [the input subject] itself. */
  outside: PackedCoords[];
  /* Intersection pieces carved out by the clips, in clip order. */
  inside: PackedCoords[];
  /* Whether any clip step actually modified the subject. */
  touched: boolean;
  /* Number of clip steps skipped because the difference operation hit a
   * topology error (the intersection pieces of a skipped step remain in
   * `inside`). */
  failures: number;
}

export interface DifferenceEachResult {
  outside: PackedCoords[];
  touched: boolean;
  failures: number;
}

export interface SplitMergedResult {
  /* union(subjects) minus union(clips) */
  outside: PackedCoords[];
  /* union(subjects) intersected with union(clips) */
  inside: PackedCoords[];
}

/* Merged bulk operation, mirroring cpp/core/polyclip.cpp splitMerged():
 * two sweeps total regardless of polygon counts, but coarser semantics
 * than splitEachJs — results are not attributed to individual subjects,
 * overlapping or touching subjects merge in the output, coordinates are
 * normalized even for untouched subjects, and any topology error fails
 * the whole operation. */
export function splitMergedJs(subjects: PackedCoords[], clips: PackedCoords[]): SplitMergedResult {
  return {
    inside: intersectionPacked(subjects, clips),
    outside: differencePacked(subjects, clips),
  };
}

export function splitEachJs(subjects: PackedCoords[], clips: PackedCoords[]): SplitResult[] {
  const validClips = clips.filter((c) => c.ringLengths.length > 0);

  const results: SplitResult[] = [];
  for (const subject of subjects) {
    const r: SplitResult = { outside: [], inside: [], touched: false, failures: 0 };
    results.push(r);
    if (subject.ringLengths.length === 0) continue;

    let current: PackedCoords[] = [subject];
    for (const clip of validClips) {
      const inter = intersectionPacked(current, [clip]);
      if (inter.length === 0) continue;
      r.inside.push(...inter);
      let diff: PackedCoords[];
      try {
        diff = differencePacked(current, inter);
      } catch {
        r.failures++;
        continue;
      }
      r.touched = true;
      if (diff.length === 0) {
        current = [];
        break;
      }
      current = diff;
    }
    r.outside = current;
  }
  return results;
}
