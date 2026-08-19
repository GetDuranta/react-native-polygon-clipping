/* C++ translation of Duranta's polygon-clipping.ts, itself derived from the
 * `polygon-clipping` npm package by Mike Fogel (MIT license). The port is
 * deliberately line-faithful: names, control flow and floating-point
 * expressions match the TypeScript so the two implementations can be
 * differential-tested against each other. Deviations from the TS original:
 *
 *  - std::set replaces the splay trees (same total orders; BST inserts
 *    compare a new key against both in-order neighbors, which preserves the
 *    event-linking side effect the algorithm relies on).
 *  - Tree removals fall back to identity search when the comparator-based
 *    erase misses (a segment's ordering can go stale after splits; the JS
 *    splay tree silently tolerated this).
 *  - Segment::comparePoint optionally (ClipOptions::robustComparePoint)
 *    uses the exact orient2d predicate (polyclip-ts behavior) instead of
 *    the original division-based test; off by default, see polyclip.h.
 *  - prevInResult / beforeState / afterState are iterative instead of
 *    recursive: their recursion depth grows with segment count, which is
 *    unsafe on mobile threads with small stacks.
 */

#include "polyclip.h"
#include "orient2d.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>

// The epsilon comparator and the intersection math must round exactly like
// the JS engine; forbid FMA contraction.
#pragma STDC FP_CONTRACT OFF

namespace polyclip {
namespace {

/* Epsilon for coordinate snapping, see polygon-clipping.ts: large enough to
 * collapse floating-point noise on geographic coordinates (~0.0001 mm),
 * small enough to be far beyond survey-grade accuracy. */
constexpr double kEpsilon = 1e-12;
constexpr double kEpsilonSq = kEpsilon * kEpsilon;

const char* kInvalidGeom = "Input geometry is not a valid Polygon or MultiPolygon";

/* FLP comparator */
int flpCmp(double a, double b) {
  // check if they're both 0
  if (-kEpsilon < a && a < kEpsilon && -kEpsilon < b && b < kEpsilon) return 0;

  // check if they're flp equal
  const double ab = a - b;
  if (ab * ab < kEpsilonSq * a * b) return 0;

  // normal comparison
  return a < b ? -1 : 1;
}

struct XY {
  double x;
  double y;
};

struct Bbox {
  XY ll;
  XY ur;
};

bool isInBbox(const Bbox& bbox, double x, double y) {
  return bbox.ll.x <= x && x <= bbox.ur.x && bbox.ll.y <= y && y <= bbox.ur.y;
}

/* Returns whether the bboxes overlap; on true, `out` is the overlap region
 * (possibly a degenerate point/line). */
bool getBboxOverlap(const Bbox& b1, const Bbox& b2, Bbox* out = nullptr) {
  if (b2.ur.x < b1.ll.x || b1.ur.x < b2.ll.x || b2.ur.y < b1.ll.y || b1.ur.y < b2.ll.y) return false;
  if (out != nullptr) {
    out->ll.x = b1.ll.x < b2.ll.x ? b2.ll.x : b1.ll.x;
    out->ur.x = b1.ur.x < b2.ur.x ? b1.ur.x : b2.ur.x;
    out->ll.y = b1.ll.y < b2.ll.y ? b2.ll.y : b1.ll.y;
    out->ur.y = b1.ur.y < b2.ur.y ? b1.ur.y : b2.ur.y;
  }
  return true;
}

/* Cross product of two vectors with first point at origin */
double crossProduct(const XY& a, const XY& b) { return a.x * b.y - a.y * b.x; }

/* Dot product of two vectors with first point at origin */
double dotProduct(const XY& a, const XY& b) { return a.x * b.x + a.y * b.y; }

/* Comparator for two vectors with same starting point */
int compareVectorAngles(const XY& basePt, const XY& endPt1, const XY& endPt2) {
  const double res = orient2d(basePt.x, basePt.y, endPt1.x, endPt1.y, endPt2.x, endPt2.y);
  if (res > 0) return -1;
  if (res < 0) return 1;
  return 0;
}

double vecLength(const XY& v) { return std::sqrt(dotProduct(v, v)); }

/* Sine of the angle from pShared -> pAngle to pShared -> pBase */
double sineOfAngle(const XY& pShared, const XY& pBase, const XY& pAngle) {
  const XY vBase{pBase.x - pShared.x, pBase.y - pShared.y};
  const XY vAngle{pAngle.x - pShared.x, pAngle.y - pShared.y};
  return crossProduct(vAngle, vBase) / vecLength(vAngle) / vecLength(vBase);
}

/* Cosine of the angle from pShared -> pAngle to pShared -> pBase */
double cosineOfAngle(const XY& pShared, const XY& pBase, const XY& pAngle) {
  const XY vBase{pBase.x - pShared.x, pBase.y - pShared.y};
  const XY vAngle{pAngle.x - pShared.x, pAngle.y - pShared.y};
  return dotProduct(vAngle, vBase) / vecLength(vAngle) / vecLength(vBase);
}

/* x coordinate where the line (pt, v) crosses the horizontal line at y;
 * false for parallel (including overlapping) lines. */
bool horizontalIntersection(const XY& pt, const XY& v, double y, XY* out) {
  if (v.y == 0) return false;
  out->x = pt.x + v.x / v.y * (y - pt.y);
  out->y = y;
  return true;
}

bool verticalIntersection(const XY& pt, const XY& v, double x, XY* out) {
  if (v.x == 0) return false;
  out->x = x;
  out->y = pt.y + v.y / v.x * (x - pt.x);
  return true;
}

/* Intersection of two lines, each defined by a base point and a vector;
 * false for parallel (including overlapping) lines. */
bool lineIntersection(const XY& pt1, const XY& v1, const XY& pt2, const XY& v2, XY* out) {
  // take some shortcuts for vertical and horizontal lines
  if (v1.x == 0) return verticalIntersection(pt2, v2, pt1.x, out);
  if (v2.x == 0) return verticalIntersection(pt1, v1, pt2.x, out);
  if (v1.y == 0) return horizontalIntersection(pt2, v2, pt1.y, out);
  if (v2.y == 0) return horizontalIntersection(pt1, v1, pt2.y, out);

  // General case, based on Schneider and Eberly pg 244.
  const double kross = crossProduct(v1, v2);
  if (kross == 0) return false;
  const XY ve{pt2.x - pt1.x, pt2.y - pt1.y};
  const double d1 = crossProduct(ve, v1) / kross;
  const double d2 = crossProduct(ve, v2) / kross;

  // take the average of the two calculations to minimize rounding error
  const double x1 = pt1.x + d2 * v1.x, x2 = pt2.x + d1 * v2.x;
  const double y1 = pt1.y + d2 * v1.y, y2 = pt2.y + d1 * v2.y;
  out->x = (x1 + x2) / 2;
  out->y = (y1 + y2) / 2;
  return true;
}

struct SweepEvent;
struct Segment;
struct RingIn;
struct PolyIn;
struct MultiPolyIn;
struct RingOut;
struct PolyOut;
struct Op;

struct PointRec {
  double x;
  double y;
  /* All sweep events sharing this point; empty vector corresponds to the
   * TS `events === undefined` state. */
  std::vector<SweepEvent*> events;
};

struct SweepEvent {
  PointRec* point = nullptr;
  bool isLeft = false;
  Segment* segment = nullptr;
  SweepEvent* otherSE = nullptr;
  SweepEvent* consumedBy = nullptr;

  static int compare(SweepEvent* a, SweepEvent* b);
  static int comparePoints(const PointRec* aPt, const PointRec* bPt) {
    if (aPt->x < bPt->x) return -1;
    if (aPt->x > bPt->x) return 1;
    if (aPt->y < bPt->y) return -1;
    if (aPt->y > bPt->y) return 1;
    return 0;
  }

  void link(SweepEvent* other);
  void checkForConsuming();
  std::vector<SweepEvent*> getAvailableLinkedEvents();
};

struct State {
  std::vector<RingIn*> rings;
  std::vector<int> windings;
  std::vector<MultiPolyIn*> multiPolys;
};

struct Segment {
  Op* op = nullptr;
  int64_t id = 0;
  SweepEvent* leftSE = nullptr;
  SweepEvent* rightSE = nullptr;
  std::vector<RingIn*> rings;
  std::vector<int> windings;
  RingOut* ringOut = nullptr;
  Segment* consumedBy = nullptr;
  Segment* prev = nullptr;

  bool prevInResultComputed = false;
  Segment* prevInResultCache = nullptr;
  bool beforeStateComputed = false;
  State* beforeStatePtr = nullptr;
  bool afterStateComputed = false;
  State afterStateCache;
  int8_t isInResultCache = -1;

  static int compare(Segment* a, Segment* b);

  void replaceRightSE(SweepEvent* newRightSE) {
    rightSE = newRightSE;
    rightSE->segment = this;
    rightSE->otherSE = leftSE;
    leftSE->otherSE = rightSE;
  }

  Bbox bbox() const {
    const double y1 = leftSE->point->y;
    const double y2 = rightSE->point->y;
    return Bbox{{leftSE->point->x, y1 < y2 ? y1 : y2}, {rightSE->point->x, y1 > y2 ? y1 : y2}};
  }

  XY vector() const {
    return XY{rightSE->point->x - leftSE->point->x, rightSE->point->y - leftSE->point->y};
  }

  bool isAnEndpoint(double px, double py) const {
    return (px == leftSE->point->x && py == leftSE->point->y) ||
           (px == rightSE->point->x && py == rightSE->point->y);
  }

  int comparePoint(double px, double py) const;
  PointRec* getIntersection(Segment* other);
  std::vector<SweepEvent*> split(PointRec* point);
  void swapEvents();
  void consume(Segment* other);
  Segment* prevInResult();
  State& beforeState();
  State& afterState();
  bool isInResult();

 private:
  void computeAfterState();
};

struct RingIn {
  PolyIn* poly = nullptr;
  bool isExterior = false;
  std::vector<Segment*> segments;
  Bbox bbox{{0, 0}, {0, 0}};
};

struct PolyIn {
  RingIn* exteriorRing = nullptr;
  Bbox bbox{{0, 0}, {0, 0}};
  std::vector<RingIn*> interiorRings;
  MultiPolyIn* multiPoly = nullptr;
};

struct MultiPolyIn {
  std::vector<PolyIn*> polys;
  Bbox bbox{{0, 0}, {0, 0}};
  bool isSubject = false;
};

struct RingOut {
  std::vector<SweepEvent*> events;
  PolyOut* poly = nullptr;
  int8_t isExteriorCache = -1;
  bool enclosingComputed = false;
  RingOut* enclosingCache = nullptr;

  int appendPackedCoords(std::vector<double>& packedCoordinates);
  bool isExteriorRing();
  RingOut* enclosingRing();
  RingOut* calcEnclosingRing();
};

struct PolyOut {
  RingOut* exteriorRing = nullptr;
  std::vector<RingOut*> interiorRings;
};

struct EventPtrLess {
  bool operator()(SweepEvent* a, SweepEvent* b) const { return SweepEvent::compare(a, b) < 0; }
};
struct SegmentPtrLess {
  bool operator()(Segment* a, Segment* b) const { return Segment::compare(a, b) < 0; }
};

using EventTree = std::set<SweepEvent*, EventPtrLess>;
using SegmentTree = std::set<Segment*, SegmentPtrLess>;

/* CoordRounder: snaps incoming values to previously-seen values that are
 * within flp-epsilon, avoiding near-coincident coordinates. */
class CoordRounder {
 public:
  CoordRounder() {
    // preseed with 0 so we don't end up with values < Number.EPSILON
    round(0);
  }

  // Note: this can round input values backwards or forwards, see the TS
  // original for why forward-only rounding wouldn't suffice.
  double round(double coord) {
    auto [it, inserted] = tree_.insert(coord);
    (void)inserted;
    if (it != tree_.begin()) {
      auto prevIt = std::prev(it);
      if (flpCmp(*it, *prevIt) == 0) {
        const double snapped = *prevIt;
        tree_.erase(it);
        return snapped;
      }
    }
    auto nextIt = std::next(it);
    if (nextIt != tree_.end() && flpCmp(*it, *nextIt) == 0) {
      const double snapped = *nextIt;
      tree_.erase(it);
      return snapped;
    }
    return coord;
  }

 private:
  std::set<double> tree_;
};

/* Per-operation context: replaces the TS module-level `operation` and
 * `rounder` singletons and owns all node allocations, making concurrent
 * clip() calls on different threads safe. */
struct Op {
  OpType type = OpType::Union;
  int numMultiPolys = 0;
  ClipOptions options;
  int64_t segmentId = 0;
  State emptyState;

  CoordRounder xRounder;
  CoordRounder yRounder;

  std::deque<PointRec> points;
  std::deque<SweepEvent> events;
  std::deque<Segment> segments;
  std::deque<RingIn> ringIns;
  std::deque<PolyIn> polyIns;
  std::deque<MultiPolyIn> multiPolyIns;
  std::deque<RingOut> ringOuts;
  std::deque<PolyOut> polyOuts;

  PointRec* roundPoint(double x, double y) {
    points.push_back(PointRec{xRounder.round(x), yRounder.round(y), {}});
    return &points.back();
  }

  SweepEvent* newEvent(PointRec* point, bool isLeft) {
    events.push_back(SweepEvent{});
    SweepEvent* evt = &events.back();
    point->events.push_back(evt);
    evt->point = point;
    evt->isLeft = isLeft;
    return evt;
  }

  Segment* newSegment(SweepEvent* leftSE, SweepEvent* rightSE, std::vector<RingIn*> rings,
                      std::vector<int> windings) {
    segments.push_back(Segment{});
    Segment* seg = &segments.back();
    seg->op = this;
    seg->id = ++segmentId;
    seg->leftSE = leftSE;
    leftSE->segment = seg;
    leftSE->otherSE = rightSE;
    seg->rightSE = rightSE;
    rightSE->segment = seg;
    rightSE->otherSE = leftSE;
    seg->rings = std::move(rings);
    seg->windings = std::move(windings);
    return seg;
  }

  Segment* segmentFromRing(PointRec* pt1, PointRec* pt2, RingIn* ring) {
    PointRec* leftPt;
    PointRec* rightPt;
    int winding;
    const int cmpPts = SweepEvent::comparePoints(pt1, pt2);
    if (cmpPts < 0) {
      leftPt = pt1;
      rightPt = pt2;
      winding = 1;
    } else if (cmpPts > 0) {
      leftPt = pt2;
      rightPt = pt1;
      winding = -1;
    } else {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "Tried to create degenerate segment at [%.17g, %.17g]", pt1->x, pt1->y);
      throw std::runtime_error(buf);
    }
    SweepEvent* leftSE = newEvent(leftPt, true);
    SweepEvent* rightSE = newEvent(rightPt, false);
    return newSegment(leftSE, rightSE, {ring}, {winding});
  }
};

/* ---- SweepEvent methods ---- */

int SweepEvent::compare(SweepEvent* a, SweepEvent* b) {
  // favor event with a point that the sweep line hits first
  const int ptCmp = comparePoints(a->point, b->point);
  if (ptCmp != 0) return ptCmp;

  // the points are the same, so link them if needed
  if (a->point != b->point) a->link(b);

  // favor right events over left
  if (a->isLeft != b->isLeft) return a->isLeft ? 1 : -1;

  // two matching left or right endpoints: order like their segments
  return Segment::compare(a->segment, b->segment);
}

void SweepEvent::link(SweepEvent* other) {
  if (other->point == point) {
    throw std::runtime_error("Tried to link already linked events");
  }
  std::vector<SweepEvent*>& otherEvents = other->point->events;
  for (size_t i = 0, iMax = otherEvents.size(); i < iMax; i++) {
    SweepEvent* evt = otherEvents[i];
    point->events.push_back(evt);
    evt->point = point;
  }
  checkForConsuming();
}

void SweepEvent::checkForConsuming() {
  // Compare each pair of events to see if their segments should be consumed.
  const size_t numEvents = point->events.size();
  for (size_t i = 0; i < numEvents; i++) {
    SweepEvent* evt1 = point->events[i];
    if (evt1->segment->consumedBy != nullptr) continue;
    for (size_t j = i + 1; j < numEvents; j++) {
      SweepEvent* evt2 = point->events[j];
      if (evt2->consumedBy != nullptr) continue;
      if (evt1->otherSE->point != evt2->otherSE->point) continue;
      evt1->segment->consume(evt2->segment);
    }
  }
}

std::vector<SweepEvent*> SweepEvent::getAvailableLinkedEvents() {
  std::vector<SweepEvent*> result;
  for (size_t i = 0, iMax = point->events.size(); i < iMax; i++) {
    SweepEvent* evt = point->events[i];
    if (evt != this && evt->segment->ringOut == nullptr && evt->segment->isInResult()) {
      result.push_back(evt);
    }
  }
  return result;
}

/* Pick the linked event that gives the smallest left-side angle. All ring
 * construction starts as low as possible heading to the right, so by always
 * turning left as sharply as possible we get polygons without unnecessary
 * loops & holes. (Replaces the TS getLeftmostComparator + sort()[0].) */
SweepEvent* leftmostLinkedEvent(SweepEvent* point, SweepEvent* baseEvent,
                                const std::vector<SweepEvent*>& candidates) {
  struct SinCos {
    double sine;
    double cosine;
  };
  std::vector<SinCos> cache(candidates.size());
  const XY pShared{point->point->x, point->point->y};
  const XY pBase{baseEvent->point->x, baseEvent->point->y};
  for (size_t i = 0; i < candidates.size(); i++) {
    const SweepEvent* nextEvent = candidates[i]->otherSE;
    const XY pAngle{nextEvent->point->x, nextEvent->point->y};
    cache[i] = SinCos{sineOfAngle(pShared, pBase, pAngle), cosineOfAngle(pShared, pBase, pAngle)};
  }
  const auto lessThan = [&](size_t ia, size_t ib) {
    const double asine = cache[ia].sine, acosine = cache[ia].cosine;
    const double bsine = cache[ib].sine, bcosine = cache[ib].cosine;
    // both on or above x-axis
    if (asine >= 0 && bsine >= 0) return acosine > bcosine;
    // both below x-axis
    if (asine < 0 && bsine < 0) return acosine < bcosine;
    // one above x-axis, one below
    return bsine < asine;
  };
  size_t best = 0;
  for (size_t i = 1; i < candidates.size(); i++) {
    if (lessThan(i, best)) best = i;
  }
  return candidates[best];
}

/* ---- Segment methods ---- */

int Segment::compare(Segment* a, Segment* b) {
  const double alx = a->leftSE->point->x;
  const double blx = b->leftSE->point->x;
  const double arx = a->rightSE->point->x;
  const double brx = b->rightSE->point->x;

  // check if they're even in the same vertical plane
  if (brx < alx) return 1;
  if (arx < blx) return -1;
  const double aly = a->leftSE->point->y;
  const double bly = b->leftSE->point->y;
  const double ary = a->rightSE->point->y;
  const double bry = b->rightSE->point->y;

  // is left endpoint of segment B the right-more?
  if (alx < blx) {
    // are the two segments in the same horizontal plane?
    if (bly < aly && bly < ary) return 1;
    if (bly > aly && bly > ary) return -1;

    // is the B left endpoint colinear to segment A?
    const int aCmpBLeft = a->comparePoint(blx, bly);
    if (aCmpBLeft < 0) return 1;
    if (aCmpBLeft > 0) return -1;

    // is the A right endpoint colinear to segment B?
    const int bCmpARight = b->comparePoint(arx, ary);
    if (bCmpARight != 0) return bCmpARight;

    // colinear segments, consider the one with left-more left endpoint first
    return -1;
  }

  // is left endpoint of segment A the right-more?
  if (alx > blx) {
    if (aly < bly && aly < bry) return -1;
    if (aly > bly && aly > bry) return 1;

    // is the A left endpoint colinear to segment B?
    const int bCmpALeft = b->comparePoint(alx, aly);
    if (bCmpALeft != 0) return bCmpALeft;

    // is the B right endpoint colinear to segment A?
    const int aCmpBRight = a->comparePoint(brx, bry);
    if (aCmpBRight < 0) return 1;
    if (aCmpBRight > 0) return -1;

    // colinear segments, consider the one with left-more left endpoint first
    return 1;
  }

  // left endpoints are in the same vertical plane, ie alx === blx
  // consider the lower left-endpoint to come first
  if (aly < bly) return -1;
  if (aly > bly) return 1;

  // left endpoints identical: check colinearity via left-more right endpoint
  if (arx < brx) {
    const int bCmpARight = b->comparePoint(arx, ary);
    if (bCmpARight != 0) return bCmpARight;
  }
  if (arx > brx) {
    const int aCmpBRight = a->comparePoint(brx, bry);
    if (aCmpBRight < 0) return 1;
    if (aCmpBRight > 0) return -1;
  }
  if (arx != brx) {
    // are these two [almost] vertical segments with opposite orientation?
    // if so, the one with the lower right endpoint comes first
    const double ay = ary - aly;
    const double ax = arx - alx;
    const double by = bry - bly;
    const double bx = brx - blx;
    if (ay > ax && by < bx) return 1;
    if (ay < ax && by > bx) return -1;
  }

  // colinear segments with matching orientation:
  // the one with more left-more right endpoint comes first
  if (arx > brx) return 1;
  if (arx < brx) return -1;

  // right endpoints in the same vertical plane, ie arx === brx
  // consider the lower right-endpoint to come first
  if (ary < bry) return -1;
  if (ary > bry) return 1;

  // identical endpoints: fall back on creation order as tie-breaker
  if (a->id < b->id) return -1;
  if (a->id > b->id) return 1;

  // identical segment, ie a === b
  return 0;
}

/* Compare this segment with a point: 1 = point above the segment (left of
 * vertical), 0 = colinear, -1 = below (right of vertical). */
int Segment::comparePoint(double px, double py) const {
  if (isAnEndpoint(px, py)) return 0;
  const PointRec* lPt = leftSE->point;
  const PointRec* rPt = rightSE->point;

  if (op->options.robustComparePoint) {
    // polyclip-ts behavior: one exact orientation predicate, consistent
    // with compareVectorAngles everywhere else in the algorithm.
    const double res = orient2d(lPt->x, lPt->y, px, py, rPt->x, rPt->y);
    if (res > 0) return 1;
    if (res < 0) return -1;
    return 0;
  }

  // Original polygon-clipping division-based test (differential testing).
  const XY v = vector();

  // Exactly vertical segments.
  if (lPt->x == rPt->x) {
    if (px == lPt->x) return 0;
    return px < lPt->x ? 1 : -1;
  }

  // Nearly vertical segments with an intersection.
  const double yDist = (py - lPt->y) / v.y;
  const double xFromYDist = lPt->x + yDist * v.x;
  if (px == xFromYDist) return 0;

  // General case.
  const double xDist = (px - lPt->x) / v.x;
  const double yFromXDist = lPt->y + xDist * v.y;
  if (py == yFromXDist) return 0;
  return py < yFromXDist ? -1 : 1;
}

/* First non-trivial intersection between the two segments in sweep line
 * order, or nullptr. See the TS original for the trivial/non-trivial
 * distinction. */
PointRec* Segment::getIntersection(Segment* other) {
  const Bbox tBbox = bbox();
  const Bbox oBbox = other->bbox();
  Bbox bboxOverlap;
  if (!getBboxOverlap(tBbox, oBbox, &bboxOverlap)) return nullptr;

  // We first check to see if the endpoints can be considered intersections.
  // This will 'snap' intersections to endpoints if possible, and will
  // handle cases of colinearity.
  PointRec* tlp = leftSE->point;
  PointRec* trp = rightSE->point;
  PointRec* olp = other->leftSE->point;
  PointRec* orp = other->rightSE->point;

  const bool touchesOtherLSE = isInBbox(tBbox, olp->x, olp->y) && comparePoint(olp->x, olp->y) == 0;
  const bool touchesThisLSE = isInBbox(oBbox, tlp->x, tlp->y) && other->comparePoint(tlp->x, tlp->y) == 0;
  const bool touchesOtherRSE = isInBbox(tBbox, orp->x, orp->y) && comparePoint(orp->x, orp->y) == 0;
  const bool touchesThisRSE = isInBbox(oBbox, trp->x, trp->y) && other->comparePoint(trp->x, trp->y) == 0;

  // do left endpoints match?
  if (touchesThisLSE && touchesOtherLSE) {
    // colinear segments with matching left endpoints, one longer than other
    if (touchesThisRSE && !touchesOtherRSE) return trp;
    if (!touchesThisRSE && touchesOtherRSE) return orp;
    // either exact match or just left endpoints: trivial
    return nullptr;
  }

  // does this left endpoint match (other doesn't)
  if (touchesThisLSE) {
    // check for segments that just intersect on opposing endpoints
    if (touchesOtherRSE && tlp->x == orp->x && tlp->y == orp->y) return nullptr;
    // t-intersection on left endpoint
    return tlp;
  }

  // does other left endpoint match (this doesn't)
  if (touchesOtherLSE) {
    if (touchesThisRSE && trp->x == olp->x && trp->y == olp->y) return nullptr;
    // t-intersection on left endpoint
    return olp;
  }

  // trivial intersection on right endpoints
  if (touchesThisRSE && touchesOtherRSE) return nullptr;

  // t-intersections on just one right endpoint
  if (touchesThisRSE) return trp;
  if (touchesOtherRSE) return orp;

  // General intersection between infinite lines laid over the segments.
  XY pt;
  const XY tlpXY{tlp->x, tlp->y};
  const XY olpXY{olp->x, olp->y};
  if (!lineIntersection(tlpXY, vector(), olpXY, other->vector(), &pt)) return nullptr;

  // is the intersection found between the lines not on the segments?
  if (!isInBbox(bboxOverlap, pt.x, pt.y)) return nullptr;

  // round the computed point if needed
  return op->roundPoint(pt.x, pt.y);
}

/* Split this segment at the given point; returns newly generated
 * SweepEvents. */
std::vector<SweepEvent*> Segment::split(PointRec* point) {
  std::vector<SweepEvent*> newEvents;
  const bool alreadyLinked = !point->events.empty();
  SweepEvent* newLeftSE = op->newEvent(point, true);
  SweepEvent* newRightSE = op->newEvent(point, false);
  SweepEvent* oldRightSE = rightSE;
  replaceRightSE(newRightSE);
  newEvents.push_back(newRightSE);
  newEvents.push_back(newLeftSE);
  Segment* newSeg = op->newSegment(newLeftSE, oldRightSE, rings, windings);

  // when splitting a nearly vertical downward-facing segment, one of the
  // new segments may come out vertical with swapped left/right events
  if (SweepEvent::comparePoints(newSeg->leftSE->point, newSeg->rightSE->point) > 0) {
    newSeg->swapEvents();
  }
  if (SweepEvent::comparePoints(leftSE->point, rightSE->point) > 0) {
    swapEvents();
  }

  // if the point was already linked to other events, check if either of
  // the affected segments should be consumed
  if (alreadyLinked) {
    newLeftSE->checkForConsuming();
    newRightSE->checkForConsuming();
  }
  return newEvents;
}

void Segment::swapEvents() {
  SweepEvent* tmpEvt = rightSE;
  rightSE = leftSE;
  leftSE = tmpEvt;
  leftSE->isLeft = true;
  rightSE->isLeft = false;
  for (size_t i = 0, iMax = windings.size(); i < iMax; i++) {
    windings[i] *= -1;
  }
}

/* Consume another segment: take their rings under our wing and mark them
 * consumed. Used for perfectly overlapping segments. */
void Segment::consume(Segment* other) {
  Segment* consumer = this;
  Segment* consumee = other;
  while (consumer->consumedBy != nullptr) consumer = consumer->consumedBy;
  while (consumee->consumedBy != nullptr) consumee = consumee->consumedBy;
  const int cmp = compare(consumer, consumee);
  if (cmp == 0) return; // already consumed
  // the winner of the consumption is the earlier segment in sweep order
  if (cmp > 0) std::swap(consumer, consumee);

  // make sure a segment doesn't consume its prev
  if (consumer->prev == consumee) std::swap(consumer, consumee);

  for (size_t i = 0, iMax = consumee->rings.size(); i < iMax; i++) {
    RingIn* ring = consumee->rings[i];
    const int winding = consumee->windings[i];
    const auto it = std::find(consumer->rings.begin(), consumer->rings.end(), ring);
    if (it == consumer->rings.end()) {
      consumer->rings.push_back(ring);
      consumer->windings.push_back(winding);
    } else {
      consumer->windings[static_cast<size_t>(it - consumer->rings.begin())] += winding;
    }
  }
  consumee->rings.clear();
  consumee->windings.clear();
  consumee->consumedBy = consumer;

  // mark sweep events consumed as to maintain ordering in sweep event queue
  consumee->leftSE->consumedBy = consumer->leftSE;
  consumee->rightSE->consumedBy = consumer->rightSE;
}

/* The first segment in the prev chain that is in the result. Iterative
 * (the TS original recurses per prev link). */
Segment* Segment::prevInResult() {
  if (prevInResultComputed) return prevInResultCache;
  std::vector<Segment*> chain;
  Segment* cur = this;
  Segment* result = nullptr;
  while (true) {
    if (cur->prevInResultComputed) {
      result = cur->prevInResultCache;
      break;
    }
    chain.push_back(cur);
    if (cur->prev == nullptr) {
      result = nullptr;
      break;
    }
    if (cur->prev->isInResult()) {
      result = cur->prev;
      break;
    }
    cur = cur->prev;
  }
  for (Segment* s : chain) {
    s->prevInResultCache = result;
    s->prevInResultComputed = true;
  }
  return result;
}

/* beforeState/afterState: iterative unwinding of the TS recursion along
 * the prev chain. beforeState aliases the prev segment's afterState (as in
 * TS, where it stores the same object reference). */
State& Segment::beforeState() {
  if (beforeStateComputed) return *beforeStatePtr;

  // Collect the chain of segments whose afterState is still uncomputed.
  std::vector<Segment*> chain{this};
  while (true) {
    Segment* p = chain.back()->prev;
    if (p == nullptr) break;
    Segment* seg = p->consumedBy != nullptr ? p->consumedBy : p;
    if (seg->afterStateComputed) break;
    chain.push_back(seg);
  }

  // Compute states from the oldest segment forward; the last chain entry
  // (this) only needs its beforeState.
  for (size_t i = chain.size(); i-- > 0;) {
    Segment* s = chain[i];
    if (!s->beforeStateComputed) {
      if (s->prev == nullptr) {
        s->beforeStatePtr = &s->op->emptyState;
      } else {
        Segment* seg = s->prev->consumedBy != nullptr ? s->prev->consumedBy : s->prev;
        s->beforeStatePtr = &seg->afterStateCache;
      }
      s->beforeStateComputed = true;
    }
    if (i > 0 && !s->afterStateComputed) s->computeAfterState();
  }
  return *beforeStatePtr;
}

State& Segment::afterState() {
  if (!afterStateComputed) {
    beforeState();
    computeAfterState();
  }
  return afterStateCache;
}

void Segment::computeAfterState() {
  const State& before = *beforeStatePtr;
  afterStateCache.rings = before.rings;
  afterStateCache.windings = before.windings;
  afterStateCache.multiPolys.clear();
  std::vector<RingIn*>& ringsAfter = afterStateCache.rings;
  std::vector<int>& windingsAfter = afterStateCache.windings;
  std::vector<MultiPolyIn*>& mpsAfter = afterStateCache.multiPolys;

  // calculate ringsAfter, windingsAfter
  for (size_t i = 0, iMax = rings.size(); i < iMax; i++) {
    RingIn* ring = rings[i];
    const int winding = windings[i];
    const auto it = std::find(ringsAfter.begin(), ringsAfter.end(), ring);
    if (it == ringsAfter.end()) {
      ringsAfter.push_back(ring);
      windingsAfter.push_back(winding);
    } else {
      windingsAfter[static_cast<size_t>(it - ringsAfter.begin())] += winding;
    }
  }

  // calculate polysAfter
  std::vector<PolyIn*> polysAfter;
  std::vector<PolyIn*> polysExclude;
  for (size_t i = 0, iMax = ringsAfter.size(); i < iMax; i++) {
    if (windingsAfter[i] == 0) continue; // non-zero rule
    RingIn* ring = ringsAfter[i];
    PolyIn* poly = ring->poly;
    if (std::find(polysExclude.begin(), polysExclude.end(), poly) != polysExclude.end()) continue;
    if (ring->isExterior) {
      polysAfter.push_back(poly);
    } else {
      if (std::find(polysExclude.begin(), polysExclude.end(), poly) == polysExclude.end()) {
        polysExclude.push_back(poly);
      }
      const auto it = std::find(polysAfter.begin(), polysAfter.end(), ring->poly);
      if (it != polysAfter.end()) polysAfter.erase(it);
    }
  }

  // calculate multiPolysAfter
  for (size_t i = 0, iMax = polysAfter.size(); i < iMax; i++) {
    MultiPolyIn* mp = polysAfter[i]->multiPoly;
    if (std::find(mpsAfter.begin(), mpsAfter.end(), mp) == mpsAfter.end()) mpsAfter.push_back(mp);
  }
  afterStateComputed = true;
}

/* Is this segment part of the final result? */
bool Segment::isInResult() {
  // if we've been consumed, we're not in the result
  if (consumedBy != nullptr) return false;
  if (isInResultCache != -1) return isInResultCache != 0;

  const std::vector<MultiPolyIn*>& mpsBefore = beforeState().multiPolys;
  const std::vector<MultiPolyIn*>& mpsAfter = afterState().multiPolys;
  bool result = false;
  switch (op->type) {
    case OpType::Union: {
      // UNION - included iff on one side there are 0 poly interiors and on
      // the other side there is 1 or more
      const bool noBefores = mpsBefore.empty();
      const bool noAfters = mpsAfter.empty();
      result = noBefores != noAfters;
      break;
    }
    case OpType::Intersection: {
      // INTERSECTION - included iff on one side all multipolys are
      // represented with poly interiors and on the other side not all are
      size_t least;
      size_t most;
      if (mpsBefore.size() < mpsAfter.size()) {
        least = mpsBefore.size();
        most = mpsAfter.size();
      } else {
        least = mpsAfter.size();
        most = mpsBefore.size();
      }
      result = most == static_cast<size_t>(op->numMultiPolys) && least < most;
      break;
    }
    case OpType::Xor: {
      // XOR - included iff the difference in the number of multipolys
      // represented on the two sides is odd
      const size_t diff = mpsBefore.size() > mpsAfter.size() ? mpsBefore.size() - mpsAfter.size()
                                                             : mpsAfter.size() - mpsBefore.size();
      result = diff % 2 == 1;
      break;
    }
    case OpType::Difference: {
      // DIFFERENCE - included iff on exactly one side we have just the subject
      const auto isJustSubject = [](const std::vector<MultiPolyIn*>& mps) {
        return mps.size() == 1 && mps[0]->isSubject;
      };
      result = isJustSubject(mpsBefore) != isJustSubject(mpsAfter);
      break;
    }
  }
  isInResultCache = result ? 1 : 0;
  return result;
}

/* ---- Input geometry construction ---- */

RingIn* ringFromPacked(Op& op, const double* coords, size_t coordPairs, int64_t startPair, int64_t count,
                       PolyIn* poly, bool isExterior) {
  if (count <= 0 || startPair + count > static_cast<int64_t>(coordPairs)) {
    throw std::invalid_argument(kInvalidGeom);
  }
  op.ringIns.push_back(RingIn{});
  RingIn* ring = &op.ringIns.back();
  ring->poly = poly;
  ring->isExterior = isExterior;

  PointRec* firstPoint = op.roundPoint(coords[startPair * 2], coords[startPair * 2 + 1]);
  ring->bbox = Bbox{{firstPoint->x, firstPoint->y}, {firstPoint->x, firstPoint->y}};

  PointRec* prevPoint = firstPoint;
  for (int64_t i = 1; i < count; i++) {
    PointRec* point = op.roundPoint(coords[(startPair + i) * 2], coords[(startPair + i) * 2 + 1]);
    // skip repeated points
    if (point->x == prevPoint->x && point->y == prevPoint->y) continue;
    ring->segments.push_back(op.segmentFromRing(prevPoint, point, ring));
    if (point->x < ring->bbox.ll.x) ring->bbox.ll.x = point->x;
    if (point->y < ring->bbox.ll.y) ring->bbox.ll.y = point->y;
    if (point->x > ring->bbox.ur.x) ring->bbox.ur.x = point->x;
    if (point->y > ring->bbox.ur.y) ring->bbox.ur.y = point->y;
    prevPoint = point;
  }
  // add segment from last to first if last is not the same as first
  if (firstPoint->x != prevPoint->x || firstPoint->y != prevPoint->y) {
    ring->segments.push_back(op.segmentFromRing(prevPoint, firstPoint, ring));
  }
  return ring;
}

PolyIn* polyFromPacked(Op& op, const PackedPolygon& packed, MultiPolyIn* multiPoly) {
  if (packed.ringLengths.empty()) throw std::invalid_argument(kInvalidGeom);
  if (packed.coords.size() % 2 != 0) throw std::invalid_argument(kInvalidGeom);
  const size_t coordPairs = packed.coords.size() / 2;

  op.polyIns.push_back(PolyIn{});
  PolyIn* poly = &op.polyIns.back();
  poly->multiPoly = multiPoly;

  int64_t startPair = 0;
  poly->exteriorRing = ringFromPacked(op, packed.coords.data(), coordPairs, startPair,
                                      packed.ringLengths[0], poly, true);
  startPair += packed.ringLengths[0];
  poly->bbox = poly->exteriorRing->bbox;
  for (size_t i = 1, iMax = packed.ringLengths.size(); i < iMax; i++) {
    RingIn* ring = ringFromPacked(op, packed.coords.data(), coordPairs, startPair,
                                  packed.ringLengths[i], poly, false);
    startPair += packed.ringLengths[i];
    if (ring->bbox.ll.x < poly->bbox.ll.x) poly->bbox.ll.x = ring->bbox.ll.x;
    if (ring->bbox.ll.y < poly->bbox.ll.y) poly->bbox.ll.y = ring->bbox.ll.y;
    if (ring->bbox.ur.x > poly->bbox.ur.x) poly->bbox.ur.x = ring->bbox.ur.x;
    if (ring->bbox.ur.y > poly->bbox.ur.y) poly->bbox.ur.y = ring->bbox.ur.y;
    poly->interiorRings.push_back(ring);
  }
  return poly;
}

MultiPolyIn* multiPolyFromPacked(Op& op, const PackedMultiPolygon& packed, bool isSubject) {
  op.multiPolyIns.push_back(MultiPolyIn{});
  MultiPolyIn* mp = &op.multiPolyIns.back();
  mp->isSubject = isSubject;
  mp->bbox = Bbox{{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()},
                  {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}};
  for (const PackedPolygon& packedPoly : packed) {
    PolyIn* poly = polyFromPacked(op, packedPoly, mp);
    if (poly->bbox.ll.x < mp->bbox.ll.x) mp->bbox.ll.x = poly->bbox.ll.x;
    if (poly->bbox.ll.y < mp->bbox.ll.y) mp->bbox.ll.y = poly->bbox.ll.y;
    if (poly->bbox.ur.x > mp->bbox.ur.x) mp->bbox.ur.x = poly->bbox.ur.x;
    if (poly->bbox.ur.y > mp->bbox.ur.y) mp->bbox.ur.y = poly->bbox.ur.y;
    mp->polys.push_back(poly);
  }
  return mp;
}

void collectSweepEvents(const MultiPolyIn* mp, std::vector<SweepEvent*>& out) {
  for (const PolyIn* poly : mp->polys) {
    const RingIn* exterior = poly->exteriorRing;
    for (const Segment* seg : exterior->segments) {
      out.push_back(seg->leftSE);
      out.push_back(seg->rightSE);
    }
    for (const RingIn* ring : poly->interiorRings) {
      for (const Segment* seg : ring->segments) {
        out.push_back(seg->leftSE);
        out.push_back(seg->rightSE);
      }
    }
  }
}

/* ---- Output geometry ---- */

std::vector<RingOut*> ringOutFactory(Op& op, const std::vector<Segment*>& allSegments) {
  std::vector<RingOut*> ringsOut;

  const auto makeRingOut = [&op](std::vector<SweepEvent*> evts) {
    op.ringOuts.push_back(RingOut{});
    RingOut* ring = &op.ringOuts.back();
    ring->events = std::move(evts);
    for (SweepEvent* evt : ring->events) evt->segment->ringOut = ring;
    return ring;
  };

  struct IntersectionLE {
    size_t index;
    PointRec* point;
  };

  for (size_t s = 0, sMax = allSegments.size(); s < sMax; s++) {
    Segment* segment = allSegments[s];
    if (!segment->isInResult() || segment->ringOut != nullptr) continue;

    SweepEvent* prevEvent = nullptr;
    SweepEvent* event = segment->leftSE;
    SweepEvent* nextEvent = segment->rightSE;
    std::vector<SweepEvent*> events{event};
    PointRec* startingPoint = event->point;
    std::vector<IntersectionLE> intersectionLEs;

    /* Walk the chain of linked events to form a closed ring */
    while (true) {
      prevEvent = event;
      event = nextEvent;
      events.push_back(event);

      /* Is the ring complete? */
      if (event->point == startingPoint) break;

      while (true) {
        std::vector<SweepEvent*> availableLEs = event->getAvailableLinkedEvents();

        /* Did we hit a dead end? Indicates an earlier malfunction. */
        if (availableLEs.empty()) {
          const PointRec* firstPt = events[0]->point;
          const PointRec* lastPt = events[events.size() - 1]->point;
          char buf[256];
          std::snprintf(buf, sizeof(buf),
                        "Unable to complete output ring starting at [%.17g, %.17g]. "
                        "Last matching segment found ends at [%.17g, %.17g].",
                        firstPt->x, firstPt->y, lastPt->x, lastPt->y);
          throw std::runtime_error(buf);
        }

        /* Only one way to go, so continue on the path */
        if (availableLEs.size() == 1) {
          nextEvent = availableLEs[0]->otherSE;
          break;
        }

        /* We must have an intersection. Check for a completed loop */
        int64_t indexLE = -1;
        for (size_t j = 0, jMax = intersectionLEs.size(); j < jMax; j++) {
          if (intersectionLEs[j].point == event->point) {
            indexLE = static_cast<int64_t>(j);
            break;
          }
        }
        /* Found a completed loop. Cut that off and make a ring */
        if (indexLE != -1) {
          const IntersectionLE intersectionLE = intersectionLEs[static_cast<size_t>(indexLE)];
          intersectionLEs.resize(static_cast<size_t>(indexLE));
          std::vector<SweepEvent*> ringEvents(events.begin() + static_cast<int64_t>(intersectionLE.index),
                                              events.end());
          events.resize(intersectionLE.index);
          ringEvents.insert(ringEvents.begin(), ringEvents[0]->otherSE);
          std::reverse(ringEvents.begin(), ringEvents.end());
          ringsOut.push_back(makeRingOut(std::move(ringEvents)));
          continue;
        }
        /* register the intersection */
        intersectionLEs.push_back(IntersectionLE{events.size(), event->point});
        /* Choose the left-most option to continue the walk */
        nextEvent = leftmostLinkedEvent(event, prevEvent, availableLEs)->otherSE;
        break;
      }
    }
    ringsOut.push_back(makeRingOut(std::move(events)));
  }
  return ringsOut;
}

/* Append this ring's simplified, closed coordinate sequence; returns the
 * number of pairs appended (0 if the ring collapsed to colinear points). */
int RingOut::appendPackedCoords(std::vector<double>& packedCoordinates) {
  // Remove superfluous points (ie extra points along a straight line)
  PointRec* prevPt = events[0]->point;
  std::vector<PointRec*> points{prevPt};
  for (size_t i = 1, iMax = events.size() - 1; i < iMax; i++) {
    PointRec* pt = events[i]->point;
    PointRec* nextPt = events[i + 1]->point;
    if (compareVectorAngles(XY{pt->x, pt->y}, XY{prevPt->x, prevPt->y}, XY{nextPt->x, nextPt->y}) == 0) continue;
    points.push_back(pt);
    prevPt = pt;
  }

  // ring was all (within rounding error of angle calc) colinear points
  if (points.size() == 1) return 0;

  // check if the starting point is necessary
  PointRec* pt = points[0];
  PointRec* nextPt = points[1];
  if (compareVectorAngles(XY{pt->x, pt->y}, XY{prevPt->x, prevPt->y}, XY{nextPt->x, nextPt->y}) == 0) {
    points.erase(points.begin());
  }
  points.push_back(points[0]);
  const int64_t size = static_cast<int64_t>(points.size());
  const int64_t step = isExteriorRing() ? 1 : -1;
  const int64_t iStart = isExteriorRing() ? 0 : size - 1;
  const int64_t iEnd = isExteriorRing() ? size : -1;
  int count = 0;
  for (int64_t i = iStart; i != iEnd; i += step) {
    packedCoordinates.push_back(points[static_cast<size_t>(i)]->x);
    packedCoordinates.push_back(points[static_cast<size_t>(i)]->y);
    count++;
  }
  return count;
}

bool RingOut::isExteriorRing() {
  if (isExteriorCache == -1) {
    RingOut* enclosing = enclosingRing();
    isExteriorCache = (enclosing != nullptr ? !enclosing->isExteriorRing() : true) ? 1 : 0;
  }
  return isExteriorCache != 0;
}

RingOut* RingOut::enclosingRing() {
  if (!enclosingComputed) {
    enclosingCache = calcEnclosingRing();
    enclosingComputed = true;
  }
  return enclosingCache;
}

/* The ring that encloses this one, if any */
RingOut* RingOut::calcEnclosingRing() {
  // start with the earlier sweep line event so that the prevSeg chain
  // doesn't lead us inside of a loop of ours
  SweepEvent* leftMostEvt = events[0];
  for (size_t i = 1, iMax = events.size(); i < iMax; i++) {
    SweepEvent* evt = events[i];
    if (SweepEvent::compare(leftMostEvt, evt) > 0) leftMostEvt = evt;
  }
  Segment* prevSeg = leftMostEvt->segment->prevInResult();
  Segment* prevPrevSeg = prevSeg != nullptr ? prevSeg->prevInResult() : nullptr;
  while (true) {
    // no segment found, thus no ring can enclose us
    if (prevSeg == nullptr) return nullptr;

    // no segments below prev segment found, thus the ring of the prev
    // segment must loop back around and enclose us
    if (prevPrevSeg == nullptr) return prevSeg->ringOut;

    // if the two segments are of different rings, the ring of the prev
    // segment must either loop around us or the ring of the prev prev seg,
    // which would make us and the ring of the prev peers
    if (prevPrevSeg->ringOut != prevSeg->ringOut) {
      if (prevPrevSeg->ringOut->enclosingRing() != prevSeg->ringOut) {
        return prevSeg->ringOut;
      }
      return prevSeg->ringOut->enclosingRing();
    }

    // two segments are from the same ring, so this was a peninsula of that
    // ring. iterate downward, keep searching
    prevSeg = prevPrevSeg->prevInResult();
    prevPrevSeg = prevSeg != nullptr ? prevSeg->prevInResult() : nullptr;
  }
}

PackedMultiPolygon composeResult(Op& op, const std::vector<RingOut*>& rings) {
  // compose polygons from output rings
  std::vector<PolyOut*> polys;
  const auto makePolyOut = [&op, &polys](RingOut* exterior) {
    op.polyOuts.push_back(PolyOut{});
    PolyOut* poly = &op.polyOuts.back();
    poly->exteriorRing = exterior;
    exterior->poly = poly;
    polys.push_back(poly);
    return poly;
  };
  for (RingOut* ring : rings) {
    if (ring->poly != nullptr) continue;
    if (ring->isExteriorRing()) {
      makePolyOut(ring);
    } else {
      RingOut* enclosingRing = ring->enclosingRing();
      if (enclosingRing->poly == nullptr) makePolyOut(enclosingRing);
      enclosingRing->poly->interiorRings.push_back(ring);
      ring->poly = enclosingRing->poly;
    }
  }

  PackedMultiPolygon result;
  for (PolyOut* poly : polys) {
    PackedPolygon packed;
    const int exteriorCount = poly->exteriorRing->appendPackedCoords(packed.coords);
    // exterior ring was all (within rounding error of angle calc) colinear points
    if (exteriorCount == 0) continue;
    packed.ringLengths.push_back(exteriorCount);
    for (RingOut* interior : poly->interiorRings) {
      const int count = interior->appendPackedCoords(packed.coords);
      // interior ring was all colinear points
      if (count == 0) continue;
      packed.ringLengths.push_back(count);
    }
    result.push_back(std::move(packed));
  }
  return result;
}

/* ---- Sweep line ---- */

/* NOTE: we must be careful not to change any segments while they are in the
 * tree — before splitting a segment that's in the tree, remove it, split,
 * re-insert. (Even though splitting *shouldn't* change a segment's correct
 * position in the sweep line tree, rounding errors mean it sometimes does.) */
class SweepLine {
 public:
  explicit SweepLine(EventTree& queue) : queue_(queue) {}

  std::vector<Segment*> segments;

  std::vector<SweepEvent*> process(SweepEvent* event) {
    Segment* segment = event->segment;
    std::vector<SweepEvent*> newEvents;

    // if we've already been consumed by another segment, clean up our body
    // parts and get out
    if (event->consumedBy != nullptr) {
      if (event->isLeft) {
        eraseEvent(queue_, event->otherSE);
      } else {
        eraseSegment(segment);
      }
      return newEvents;
    }

    SegmentTree::iterator node;
    if (event->isLeft) {
      node = tree_.insert(segment).first;
    } else {
      node = tree_.find(segment);
      if (node == tree_.end()) {
        // The comparator-based lookup can miss if tree order went stale
        // (splits mutate ordering); fall back to identity search instead of
        // failing hard like the TS original.
        node = std::find(tree_.begin(), tree_.end(), segment);
      }
      if (node == tree_.end()) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "Unable to find segment #%lld [%.17g, %.17g] -> [%.17g, %.17g] in SweepLine tree.",
                      static_cast<long long>(segment->id), segment->leftSE->point->x,
                      segment->leftSE->point->y, segment->rightSE->point->x, segment->rightSE->point->y);
        throw std::runtime_error(buf);
      }
    }

    // skip consumed segments still in tree
    Segment* prevSeg = nullptr;
    {
      auto pit = node;
      while (pit != tree_.begin()) {
        --pit;
        if ((*pit)->consumedBy == nullptr) {
          prevSeg = *pit;
          break;
        }
      }
    }
    Segment* nextSeg = nullptr;
    {
      auto nit = std::next(node);
      while (nit != tree_.end() && (*nit)->consumedBy != nullptr) ++nit;
      if (nit != tree_.end()) nextSeg = *nit;
    }

    if (event->isLeft) {
      // Check for intersections against the previous segment in the sweep line
      PointRec* prevMySplitter = nullptr;
      if (prevSeg != nullptr) {
        PointRec* prevInter = prevSeg->getIntersection(segment);
        if (prevInter != nullptr) {
          if (!segment->isAnEndpoint(prevInter->x, prevInter->y)) prevMySplitter = prevInter;
          if (!prevSeg->isAnEndpoint(prevInter->x, prevInter->y)) {
            splitSafely(prevSeg, prevInter, newEvents);
          }
        }
      }

      // Check for intersections against the next segment in the sweep line
      PointRec* nextMySplitter = nullptr;
      if (nextSeg != nullptr) {
        PointRec* nextInter = nextSeg->getIntersection(segment);
        if (nextInter != nullptr) {
          if (!segment->isAnEndpoint(nextInter->x, nextInter->y)) nextMySplitter = nextInter;
          if (!nextSeg->isAnEndpoint(nextInter->x, nextInter->y)) {
            splitSafely(nextSeg, nextInter, newEvents);
          }
        }
      }

      // For simplicity, even if we find more than one intersection we only
      // split on the 'earliest' (sweep-line style) of them. The other
      // intersection will be handled in a future process().
      if (prevMySplitter != nullptr || nextMySplitter != nullptr) {
        PointRec* mySplitter;
        if (prevMySplitter == nullptr) {
          mySplitter = nextMySplitter;
        } else if (nextMySplitter == nullptr) {
          mySplitter = prevMySplitter;
        } else {
          const int cmpSplitters = SweepEvent::comparePoints(prevMySplitter, nextMySplitter);
          mySplitter = cmpSplitters <= 0 ? prevMySplitter : nextMySplitter;
        }

        // Rounding errors can cause changes in ordering, so remove affected
        // segments and right sweep events before splitting
        eraseEvent(queue_, segment->rightSE);
        newEvents.push_back(segment->rightSE);
        const std::vector<SweepEvent*> newEventsFromSplit = segment->split(mySplitter);
        newEvents.insert(newEvents.end(), newEventsFromSplit.begin(), newEventsFromSplit.end());
      }

      if (!newEvents.empty()) {
        // We found some intersections, so re-do the current event to make
        // sure sweep line ordering is totally consistent for later use with
        // the segment 'prev' pointers
        tree_.erase(node);
        newEvents.push_back(event);
      } else {
        // done with left event
        segments.push_back(segment);
        segment->prev = prevSeg;
      }
    } else {
      // event.isRight: since we're about to be removed from the sweep line,
      // check for intersections between our previous and next segments
      if (prevSeg != nullptr && nextSeg != nullptr) {
        PointRec* inter = prevSeg->getIntersection(nextSeg);
        if (inter != nullptr) {
          if (!prevSeg->isAnEndpoint(inter->x, inter->y)) splitSafely(prevSeg, inter, newEvents);
          if (!nextSeg->isAnEndpoint(inter->x, inter->y)) splitSafely(nextSeg, inter, newEvents);
        }
      }
      tree_.erase(node);
    }
    return newEvents;
  }

  static void eraseEvent(EventTree& queue, SweepEvent* evt) {
    if (queue.erase(evt) == 0) {
      // tolerate stale ordering: locate by identity
      const auto it = std::find(queue.begin(), queue.end(), evt);
      if (it != queue.end()) queue.erase(it);
    }
  }

 private:
  void eraseSegment(Segment* seg) {
    if (tree_.erase(seg) == 0) {
      const auto it = std::find(tree_.begin(), tree_.end(), seg);
      if (it != tree_.end()) tree_.erase(it);
    }
  }

  /* Safely split a segment that is currently in the datastructures — i.e.
   * a segment other than the one currently being processed. */
  void splitSafely(Segment* seg, PointRec* pt, std::vector<SweepEvent*>& out) {
    // Rounding errors can cause changes in ordering, so remove affected
    // segments and right sweep events before splitting
    eraseSegment(seg);
    SweepEvent* rightSE = seg->rightSE;
    eraseEvent(queue_, rightSE);
    const std::vector<SweepEvent*> newEvents = seg->split(pt);
    out.insert(out.end(), newEvents.begin(), newEvents.end());
    out.push_back(rightSE);
    // splitting can trigger consumption
    if (seg->consumedBy == nullptr) tree_.insert(seg);
  }

  EventTree& queue_;
  SegmentTree tree_;
};

} // namespace

/* ---- Public entry point ---- */

PackedMultiPolygon clip(OpType type, const PackedMultiPolygon& subject,
                        const std::vector<PackedMultiPolygon>& clips, const ClipOptions& options) {
  Op op;
  op.type = type;
  op.options = options;

  /* Convert inputs to MultiPoly objects */
  std::vector<MultiPolyIn*> multipolys{multiPolyFromPacked(op, subject, true)};
  for (const PackedMultiPolygon& packedClip : clips) {
    multipolys.push_back(multiPolyFromPacked(op, packedClip, false));
  }
  op.numMultiPolys = static_cast<int>(multipolys.size());

  /* BBox optimization for difference operation: drop clipping multipolys
   * whose bbox doesn't touch the subject's bbox at all */
  if (type == OpType::Difference) {
    const MultiPolyIn* subjectMp = multipolys[0];
    size_t i = 1;
    while (i < multipolys.size()) {
      if (getBboxOverlap(multipolys[i]->bbox, subjectMp->bbox)) {
        i++;
      } else {
        multipolys.erase(multipolys.begin() + static_cast<int64_t>(i));
      }
    }
  }

  /* BBox optimization for intersection operation: if any pair of multipolys
   * has non-overlapping bboxes, the result is empty */
  if (type == OpType::Intersection) {
    for (size_t i = 0, iMax = multipolys.size(); i < iMax; i++) {
      const MultiPolyIn* mpA = multipolys[i];
      for (size_t j = i + 1, jMax = multipolys.size(); j < jMax; j++) {
        if (!getBboxOverlap(mpA->bbox, multipolys[j]->bbox)) return {};
      }
    }
  }

  /* Put segment endpoints in a priority queue */
  EventTree queue;
  for (const MultiPolyIn* mp : multipolys) {
    std::vector<SweepEvent*> sweepEvents;
    collectSweepEvents(mp, sweepEvents);
    for (SweepEvent* evt : sweepEvents) {
      queue.insert(evt);
      if (static_cast<int64_t>(queue.size()) > options.maxQueueSize) {
        throw std::runtime_error(
            "Infinite loop when putting segment endpoints in a priority queue (queue size too big).");
      }
    }
  }

  /* Pass the sweep line over those endpoints */
  SweepLine sweepLine(queue);
  while (!queue.empty()) {
    SweepEvent* evt = *queue.begin();
    queue.erase(queue.begin());

    if (static_cast<int64_t>(queue.size()) > options.maxQueueSize) {
      throw std::runtime_error(
          "Infinite loop when passing sweep line over endpoints (queue size too big).");
    }
    if (static_cast<int64_t>(sweepLine.segments.size()) > options.maxSweepLineSegments) {
      throw std::runtime_error(
          "Infinite loop when passing sweep line over endpoints (too many sweep line segments).");
    }

    const std::vector<SweepEvent*> newEvents = sweepLine.process(evt);
    for (SweepEvent* newEvt : newEvents) {
      if (newEvt->consumedBy == nullptr) queue.insert(newEvt);
    }
  }

  /* Collect and compile segments we're keeping into a multipolygon */
  const std::vector<RingOut*> ringsOut = ringOutFactory(op, sweepLine.segments);
  return composeResult(op, ringsOut);
}

} // namespace polyclip
