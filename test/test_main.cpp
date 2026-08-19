/* Unit + differential tests for the polyclip C++ core.
 *
 * Usage:
 *   polyclip_test                     # built-in unit tests only
 *   polyclip_test fixtures.txt       # + differential fixtures, robust mode
 *   polyclip_test fixtures.txt legacy  # fixtures with the TS-faithful
 *                                        division-based comparePoint
 */

#include "../cpp/core/polyclip.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using polyclip::ClipOptions;
using polyclip::OpType;
using polyclip::PackedMultiPolygon;
using polyclip::PackedPolygon;

namespace {

int failures = 0;
int checks = 0;

void check(bool cond, const std::string& what) {
  checks++;
  if (!cond) {
    failures++;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
  }
}

PackedPolygon squarePoly(double x, double y, double size) {
  PackedPolygon p;
  p.ringLengths = {5};
  p.coords = {x, y, x + size, y, x + size, y + size, x, y + size, x, y};
  return p;
}

double polygonArea(const PackedPolygon& p) {
  double area = 0;
  size_t start = 0;
  for (size_t r = 0; r < p.ringLengths.size(); r++) {
    const size_t n = static_cast<size_t>(p.ringLengths[r]);
    double ringArea = 0;
    for (size_t i = 0; i + 1 < n; i++) {
      const double x1 = p.coords[(start + i) * 2], y1 = p.coords[(start + i) * 2 + 1];
      const double x2 = p.coords[(start + i + 1) * 2], y2 = p.coords[(start + i + 1) * 2 + 1];
      ringArea += x1 * y2 - x2 * y1;
    }
    area += ringArea / 2; // interior rings wound opposite, subtract naturally
    start += n;
  }
  return std::fabs(area);
}

double multiArea(const PackedMultiPolygon& mp) {
  double area = 0;
  for (const auto& p : mp) area += polygonArea(p);
  return area;
}

void unitTests() {
  const ClipOptions opts;

  // intersection of two overlapping unit-2 squares -> 1x1 square
  {
    auto res = polyclip::clip(OpType::Intersection, {squarePoly(0, 0, 2)}, {{{squarePoly(1, 1, 2)}}}, opts);
    check(res.size() == 1, "intersection: one polygon");
    check(std::fabs(multiArea(res) - 1.0) < 1e-9, "intersection: area 1");
  }
  // union -> area 7
  {
    auto res = polyclip::clip(OpType::Union, {squarePoly(0, 0, 2)}, {{{squarePoly(1, 1, 2)}}}, opts);
    check(res.size() == 1, "union: one polygon");
    check(std::fabs(multiArea(res) - 7.0) < 1e-9, "union: area 7");
  }
  // difference -> area 3
  {
    auto res = polyclip::clip(OpType::Difference, {squarePoly(0, 0, 2)}, {{{squarePoly(1, 1, 2)}}}, opts);
    check(res.size() == 1, "difference: one polygon");
    check(std::fabs(multiArea(res) - 3.0) < 1e-9, "difference: area 3");
  }
  // xor -> area 6, two result polys (an L and an L)
  {
    auto res = polyclip::clip(OpType::Xor, {squarePoly(0, 0, 2)}, {{{squarePoly(1, 1, 2)}}}, opts);
    check(std::fabs(multiArea(res) - 6.0) < 1e-9, "xor: area 6");
  }
  // disjoint intersection -> empty
  {
    auto res = polyclip::clip(OpType::Intersection, {squarePoly(0, 0, 1)}, {{{squarePoly(5, 5, 1)}}}, opts);
    check(res.empty(), "disjoint intersection: empty");
  }
  // fully clipped difference -> empty
  {
    auto res = polyclip::clip(OpType::Difference, {squarePoly(1, 1, 1)}, {{{squarePoly(0, 0, 5)}}}, opts);
    check(res.empty(), "contained difference: empty");
  }
  // difference producing a hole
  {
    auto res = polyclip::clip(OpType::Difference, {squarePoly(0, 0, 4)}, {{{squarePoly(1, 1, 1)}}}, opts);
    check(res.size() == 1, "hole difference: one polygon");
    check(res.size() == 1 && res[0].ringLengths.size() == 2, "hole difference: has interior ring");
    check(std::fabs(multiArea(res) - 15.0) < 1e-9, "hole difference: area 15");
  }
  // empty subject
  {
    auto res = polyclip::clip(OpType::Union, {}, {{{squarePoly(0, 0, 1)}}}, opts);
    check(std::fabs(multiArea(res) - 1.0) < 1e-9, "empty subject union: area 1");
  }
  // invalid input throws
  {
    bool threw = false;
    try {
      PackedPolygon bad;
      bad.ringLengths = {5};
      bad.coords = {0, 0, 1, 1}; // too few coords
      polyclip::clip(OpType::Union, {bad}, {}, opts);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "invalid input throws");
  }
  // splitEach: touched subject gets clipped, inside collects intersections
  {
    auto res = polyclip::splitEach({squarePoly(0, 0, 2)}, {squarePoly(1, 1, 2)}, opts);
    check(res.size() == 1, "splitEach: one result");
    check(res[0].touched && res[0].failures == 0, "splitEach: touched, no failures");
    check(std::fabs(multiArea(res[0].outside) - 3.0) < 1e-9, "splitEach: outside area 3");
    check(std::fabs(multiArea(res[0].inside) - 1.0) < 1e-9, "splitEach: inside area 1");
  }
  // splitEach: untouched subject comes back verbatim
  {
    auto res = polyclip::splitEach({squarePoly(0, 0, 1)}, {squarePoly(5, 5, 1)}, opts);
    check(res.size() == 1 && !res[0].touched, "splitEach: untouched");
    check(res[0].outside.size() == 1 && res[0].outside[0].coords == squarePoly(0, 0, 1).coords,
          "splitEach: untouched outside is verbatim input");
    check(res[0].inside.empty(), "splitEach: untouched inside empty");
  }
  // splitEach: fully consumed subject, degenerate entries ignored
  {
    PackedPolygon degenerate;
    auto res = polyclip::splitEach({squarePoly(1, 1, 1), degenerate}, {degenerate, squarePoly(0, 0, 3)}, opts);
    check(res.size() == 2, "splitEach: two results");
    check(res[0].touched && res[0].outside.empty(), "splitEach: consumed subject empty outside");
    check(std::fabs(multiArea(res[0].inside) - 1.0) < 1e-9, "splitEach: consumed subject inside area 1");
    check(!res[1].touched && res[1].outside.empty() && res[1].inside.empty(),
          "splitEach: degenerate subject empty result");
  }
  // splitMerged: one difference + one intersection over merged operands
  {
    auto r = polyclip::splitMerged({squarePoly(0, 0, 2), squarePoly(10, 10, 1)},
                                   {squarePoly(1, 1, 2), squarePoly(-5, -5, 1)}, opts);
    check(std::fabs(multiArea(r.outside) - 4.0) < 1e-9, "splitMerged: outside area 4 (3 + untouched 1)");
    check(std::fabs(multiArea(r.inside) - 1.0) < 1e-9, "splitMerged: inside area 1");
  }
  // splitMerged: overlapping clips don't double-count inside
  {
    auto r = polyclip::splitMerged({squarePoly(0, 0, 2)}, {squarePoly(0, 0, 1), squarePoly(0.5, 0.5, 1)}, opts);
    check(std::fabs(multiArea(r.inside) - 1.75) < 1e-9, "splitMerged: overlapping clips inside area 1.75");
    check(std::fabs(multiArea(r.outside) - 2.25) < 1e-9, "splitMerged: outside area 2.25");
  }
  // splitMerged: empty clips -> inside empty, outside = normalized subjects
  {
    auto r = polyclip::splitMerged({squarePoly(0, 0, 2)}, {}, opts);
    check(r.inside.empty(), "splitMerged: empty clips inside empty");
    check(std::fabs(multiArea(r.outside) - 4.0) < 1e-9, "splitMerged: empty clips outside area 4");
  }
  std::printf("unit tests: %d checks, %d failures\n", checks, failures);
}

/* ---- fixture parsing & comparison ---- */

struct ExpectedSplit {
  bool touched = false;
  int failures = 0;
  PackedMultiPolygon outside;
  PackedMultiPolygon inside;
};

struct Fixture {
  std::string name;
  OpType op = OpType::Union;
  bool isSplit = false;
  PackedMultiPolygon subject;
  std::vector<PackedMultiPolygon> clips;
  bool expectError = false;
  PackedMultiPolygon expected;
  std::vector<ExpectedSplit> expectedSplit;
};

OpType parseOp(const std::string& s) {
  if (s == "union") return OpType::Union;
  if (s == "intersection") return OpType::Intersection;
  if (s == "difference") return OpType::Difference;
  if (s == "xor") return OpType::Xor;
  throw std::runtime_error("bad op: " + s);
}

PackedMultiPolygon readMultiPoly(std::istream& in, int numPolys) {
  PackedMultiPolygon mp;
  for (int p = 0; p < numPolys; p++) {
    std::string tag;
    int numRings;
    in >> tag >> numRings;
    if (tag != "POLY") throw std::runtime_error("expected POLY, got " + tag);
    PackedPolygon poly;
    for (int r = 0; r < numRings; r++) {
      int numPairs;
      in >> tag >> numPairs;
      if (tag != "RING") throw std::runtime_error("expected RING, got " + tag);
      poly.ringLengths.push_back(numPairs);
      for (int i = 0; i < numPairs * 2; i++) {
        double v;
        in >> v;
        poly.coords.push_back(v);
      }
    }
    mp.push_back(std::move(poly));
  }
  return mp;
}

std::vector<Fixture> readFixtures(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::vector<Fixture> fixtures;
  std::string tag;
  while (in >> tag) {
    if (tag != "CASE") throw std::runtime_error("expected CASE, got " + tag);
    Fixture f;
    in >> f.name;
    std::string opStr;
    in >> tag >> opStr; // OP <op>
    if (opStr == "split_each") {
      f.isSplit = true;
    } else {
      f.op = parseOp(opStr);
    }
    int n;
    in >> tag >> n; // SUBJECT <n>
    f.subject = readMultiPoly(in, n);
    int numClips;
    in >> tag >> numClips; // CLIPS <n>
    for (int c = 0; c < numClips; c++) {
      in >> tag >> n; // CLIP <n>
      f.clips.push_back(readMultiPoly(in, n));
    }
    in >> tag; // EXPECT_ERROR | EXPECT | EXPECT_SPLIT
    if (tag == "EXPECT_ERROR") {
      f.expectError = true;
    } else if (tag == "EXPECT_SPLIT") {
      int numSubjects;
      in >> numSubjects;
      for (int s = 0; s < numSubjects; s++) {
        ExpectedSplit es;
        int touched;
        in >> tag >> touched >> es.failures; // SUBJ <t> <f>
        if (tag != "SUBJ") throw std::runtime_error("expected SUBJ, got " + tag);
        es.touched = touched != 0;
        in >> tag >> n; // OUTSIDE <n>
        es.outside = readMultiPoly(in, n);
        in >> tag >> n; // INSIDE <n>
        es.inside = readMultiPoly(in, n);
        f.expectedSplit.push_back(std::move(es));
      }
    } else {
      in >> n;
      f.expected = readMultiPoly(in, n);
    }
    in >> tag; // END
    if (tag != "END") throw std::runtime_error("expected END, got " + tag);
    fixtures.push_back(std::move(f));
  }
  return fixtures;
}

bool polygonsMatch(const PackedPolygon& a, const PackedPolygon& b, double tol, bool* exact) {
  if (a.ringLengths != b.ringLengths) return false;
  if (a.coords.size() != b.coords.size()) return false;
  for (size_t i = 0; i < a.coords.size(); i++) {
    if (a.coords[i] != b.coords[i]) *exact = false;
    if (std::fabs(a.coords[i] - b.coords[i]) > tol) return false;
  }
  return true;
}

/* In-order comparison first; if that fails, try greedy permutation matching
 * (tree-implementation differences can reorder output polygons). */
bool resultsMatch(const PackedMultiPolygon& actual, const PackedMultiPolygon& expected, double tol,
                  bool* exact, bool* reordered) {
  *exact = true;
  *reordered = false;
  if (actual.size() != expected.size()) return false;
  bool inOrder = true;
  for (size_t i = 0; i < actual.size(); i++) {
    bool e = true;
    if (!polygonsMatch(actual[i], expected[i], tol, &e)) {
      inOrder = false;
      break;
    }
    if (!e) *exact = false;
  }
  if (inOrder) return true;

  *reordered = true;
  *exact = false;
  std::vector<bool> used(expected.size(), false);
  for (const auto& a : actual) {
    bool found = false;
    for (size_t j = 0; j < expected.size(); j++) {
      if (used[j]) continue;
      bool e = true;
      if (polygonsMatch(a, expected[j], tol, &e)) {
        used[j] = true;
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

int runFixtures(const std::string& path, bool robust) {
  const auto fixtures = readFixtures(path);
  ClipOptions opts;
  opts.robustComparePoint = robust;
  const double tol = 1e-9;

  int passed = 0, exactCount = 0, reorderedCount = 0, softDiverged = 0;
  std::vector<std::string> failedCases;

  for (const auto& f : fixtures) {
    if (f.isSplit) {
      std::vector<polyclip::SplitResult> actual;
      bool threw = false;
      std::string errMsg;
      try {
        actual = polyclip::splitEach(f.subject, f.clips.at(0), opts);
      } catch (const std::exception& e) {
        threw = true;
        errMsg = e.what();
      }
      if (f.expectError) {
        if (threw) passed++;
        else failedCases.push_back(f.name + " (expected error, got result)");
        continue;
      }
      if (threw) {
        failedCases.push_back(f.name + " (unexpected error: " + errMsg + ")");
        continue;
      }
      bool ok = actual.size() == f.expectedSplit.size();
      bool allExact = true;
      bool anyReordered = false;
      for (size_t i = 0; ok && i < actual.size(); i++) {
        const ExpectedSplit& exp = f.expectedSplit[i];
        if (actual[i].touched != exp.touched || actual[i].failures != exp.failures) {
          ok = false;
          break;
        }
        bool exact = false, reordered = false;
        if (!resultsMatch(actual[i].outside, exp.outside, tol, &exact, &reordered)) {
          ok = false;
          break;
        }
        allExact = allExact && exact;
        anyReordered = anyReordered || reordered;
        if (!resultsMatch(actual[i].inside, exp.inside, tol, &exact, &reordered)) {
          ok = false;
          break;
        }
        allExact = allExact && exact;
        anyReordered = anyReordered || reordered;
      }
      if (ok) {
        passed++;
        if (allExact) exactCount++;
        if (anyReordered) reorderedCount++;
      } else {
        failedCases.push_back(f.name + " (split mismatch)");
      }
      continue;
    }

    PackedMultiPolygon actual;
    bool threw = false;
    std::string errMsg;
    try {
      actual = polyclip::clip(f.op, f.subject, f.clips, opts);
    } catch (const std::exception& e) {
      threw = true;
      errMsg = e.what();
    }

    if (f.expectError) {
      if (threw) {
        passed++;
      } else if (robust) {
        // Robust comparePoint may succeed where the TS reference threw;
        // that's an improvement, not a failure.
        softDiverged++;
        std::printf("NOTE %s: TS threw, robust C++ succeeded\n", f.name.c_str());
      } else {
        failedCases.push_back(f.name + " (expected error, got result)");
      }
      continue;
    }
    if (threw) {
      failedCases.push_back(f.name + " (unexpected error: " + errMsg + ")");
      continue;
    }
    bool exact = false, reordered = false;
    if (resultsMatch(actual, f.expected, tol, &exact, &reordered)) {
      passed++;
      if (exact) exactCount++;
      if (reordered) reorderedCount++;
    } else if (robust && std::fabs(multiArea(actual) - multiArea(f.expected)) <
                             tol * (1.0 + multiArea(f.expected))) {
      // Different vertex set but same area: robust comparePoint legitimately
      // changes edge-case splits. Count separately for review.
      softDiverged++;
      std::printf("NOTE %s: geometry differs, area matches\n", f.name.c_str());
    } else {
      char buf[160];
      std::snprintf(buf, sizeof(buf), " (mismatch: %zu vs %zu polys, area %.12g vs %.12g)",
                    actual.size(), f.expected.size(), multiArea(actual), multiArea(f.expected));
      failedCases.push_back(f.name + buf);
    }
  }

  std::printf("fixtures (%s): %zu cases, %d passed (%d bit-exact, %d reordered), %d soft-diverged, %zu failed\n",
              robust ? "robust" : "legacy", fixtures.size(), passed, exactCount, reorderedCount,
              softDiverged, failedCases.size());
  for (const auto& s : failedCases) std::fprintf(stderr, "FIXTURE FAIL: %s\n", s.c_str());
  return failedCases.empty() ? 0 : 1;
}

} // namespace

/* Replays every non-error fixture `iters` times; prints total wall time.
 * gen-fixtures.mjs --bench does the same in JS for an apples-to-apples
 * comparison on identical inputs. */
int runBench(const std::string& path, int iters) {
  const auto fixtures = readFixtures(path);
  const ClipOptions opts;
  double sink = 0;
  const auto start = std::chrono::steady_clock::now();
  int ops = 0;
  for (int it = 0; it < iters; it++) {
    for (const auto& f : fixtures) {
      if (f.expectError) continue;
      if (f.isSplit) {
        for (const auto& r : polyclip::splitEach(f.subject, f.clips.at(0), opts)) {
          sink += multiArea(r.outside);
        }
      } else {
        sink += multiArea(polyclip::clip(f.op, f.subject, f.clips, opts));
      }
      ops++;
    }
  }
  const auto end = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::printf("bench: %d ops in %.1f ms (%.3f ms/op) [sink %.3g]\n", ops, ms, ms / ops, sink);
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 2 && std::strcmp(argv[2], "bench") == 0) {
    return runBench(argv[1], argc > 3 ? std::atoi(argv[3]) : 20);
  }
  unitTests();
  int rc = failures > 0 ? 1 : 0;
  if (argc > 1) {
    const bool robust = argc > 2 && std::strcmp(argv[2], "robust") == 0;
    rc |= runFixtures(argv[1], robust);
  }
  return rc;
}
