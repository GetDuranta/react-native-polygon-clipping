/* Ported from the `robust-predicates` npm package (esm/orient2d.js and
 * esm/util.js), ISC license, (c) 2019 Volodymyr Agafonkin. Based on
 * Jonathan Shewchuk's public-domain adaptive-precision floating-point
 * predicates. The port is mechanical: every arithmetic expression is kept
 * verbatim so results are bit-identical with the JS implementation. */

#include "orient2d.h"

#include <cmath>

// The error-free transformations below rely on each +,-,* rounding exactly
// once. Forbid FMA contraction regardless of build flags.
#pragma STDC FP_CONTRACT OFF

namespace polyclip {
namespace {

constexpr double kEpsilon = 1.1102230246251565e-16;
constexpr double kSplitter = 134217729.0;
constexpr double kResulterrbound = (3 + 8 * kEpsilon) * kEpsilon;
constexpr double kCcwerrboundA = (3 + 16 * kEpsilon) * kEpsilon;
constexpr double kCcwerrboundB = (2 + 12 * kEpsilon) * kEpsilon;
constexpr double kCcwerrboundC = (9 + 64 * kEpsilon) * kEpsilon * kEpsilon;

// fast_expansion_sum_zeroelim
int expansionSum(int elen, const double* e, int flen, const double* f, double* h) {
  double Q, Qnew, hh, bvirt;
  double enow = e[0];
  double fnow = f[0];
  // In the JS original, e[++eindex] reads one past the end (yielding
  // `undefined`, never used). Guard those loads with a 0.0 sentinel.
  const auto eAt = [&](int i) { return i < elen ? e[i] : 0.0; };
  const auto fAt = [&](int i) { return i < flen ? f[i] : 0.0; };
  int eindex = 0;
  int findex = 0;
  if ((fnow > enow) == (fnow > -enow)) {
    Q = enow;
    enow = eAt(++eindex);
  } else {
    Q = fnow;
    fnow = fAt(++findex);
  }
  int hindex = 0;
  if (eindex < elen && findex < flen) {
    if ((fnow > enow) == (fnow > -enow)) {
      Qnew = enow + Q;
      hh = Q - (Qnew - enow);
      enow = eAt(++eindex);
    } else {
      Qnew = fnow + Q;
      hh = Q - (Qnew - fnow);
      fnow = fAt(++findex);
    }
    Q = Qnew;
    if (hh != 0) h[hindex++] = hh;
    while (eindex < elen && findex < flen) {
      if ((fnow > enow) == (fnow > -enow)) {
        Qnew = Q + enow;
        bvirt = Qnew - Q;
        hh = Q - (Qnew - bvirt) + (enow - bvirt);
        enow = eAt(++eindex);
      } else {
        Qnew = Q + fnow;
        bvirt = Qnew - Q;
        hh = Q - (Qnew - bvirt) + (fnow - bvirt);
        fnow = fAt(++findex);
      }
      Q = Qnew;
      if (hh != 0) h[hindex++] = hh;
    }
  }
  while (eindex < elen) {
    Qnew = Q + enow;
    bvirt = Qnew - Q;
    hh = Q - (Qnew - bvirt) + (enow - bvirt);
    enow = eAt(++eindex);
    Q = Qnew;
    if (hh != 0) h[hindex++] = hh;
  }
  while (findex < flen) {
    Qnew = Q + fnow;
    bvirt = Qnew - Q;
    hh = Q - (Qnew - bvirt) + (fnow - bvirt);
    fnow = fAt(++findex);
    Q = Qnew;
    if (hh != 0) h[hindex++] = hh;
  }
  if (Q != 0 || hindex == 0) h[hindex++] = Q;
  return hindex;
}

double estimate(int elen, const double* e) {
  double Q = e[0];
  for (int i = 1; i < elen; i++) Q += e[i];
  return Q;
}

double orient2dadapt(double ax, double ay, double bx, double by, double cx, double cy, double detsum) {
  double B[4], C1[8], C2[12], D[16], u[4];
  double acxtail, acytail, bcxtail, bcytail;
  double bvirt, c, ahi, alo, bhi, blo, _i, _j, _0, s1, s0, t1, t0, u3;

  const double acx = ax - cx;
  const double bcx = bx - cx;
  const double acy = ay - cy;
  const double bcy = by - cy;

  s1 = acx * bcy;
  c = kSplitter * acx;
  ahi = c - (c - acx);
  alo = acx - ahi;
  c = kSplitter * bcy;
  bhi = c - (c - bcy);
  blo = bcy - bhi;
  s0 = alo * blo - (s1 - ahi * bhi - alo * bhi - ahi * blo);
  t1 = acy * bcx;
  c = kSplitter * acy;
  ahi = c - (c - acy);
  alo = acy - ahi;
  c = kSplitter * bcx;
  bhi = c - (c - bcx);
  blo = bcx - bhi;
  t0 = alo * blo - (t1 - ahi * bhi - alo * bhi - ahi * blo);
  _i = s0 - t0;
  bvirt = s0 - _i;
  B[0] = s0 - (_i + bvirt) + (bvirt - t0);
  _j = s1 + _i;
  bvirt = _j - s1;
  _0 = s1 - (_j - bvirt) + (_i - bvirt);
  _i = _0 - t1;
  bvirt = _0 - _i;
  B[1] = _0 - (_i + bvirt) + (bvirt - t1);
  u3 = _j + _i;
  bvirt = u3 - _j;
  B[2] = _j - (u3 - bvirt) + (_i - bvirt);
  B[3] = u3;

  double det = estimate(4, B);
  double errbound = kCcwerrboundB * detsum;
  if (det >= errbound || -det >= errbound) {
    return det;
  }

  bvirt = ax - acx;
  acxtail = ax - (acx + bvirt) + (bvirt - cx);
  bvirt = bx - bcx;
  bcxtail = bx - (bcx + bvirt) + (bvirt - cx);
  bvirt = ay - acy;
  acytail = ay - (acy + bvirt) + (bvirt - cy);
  bvirt = by - bcy;
  bcytail = by - (bcy + bvirt) + (bvirt - cy);

  if (acxtail == 0 && acytail == 0 && bcxtail == 0 && bcytail == 0) {
    return det;
  }

  errbound = kCcwerrboundC * detsum + kResulterrbound * std::fabs(det);
  det += (acx * bcytail + bcy * acxtail) - (acy * bcxtail + bcx * acytail);
  if (det >= errbound || -det >= errbound) return det;

  s1 = acxtail * bcy;
  c = kSplitter * acxtail;
  ahi = c - (c - acxtail);
  alo = acxtail - ahi;
  c = kSplitter * bcy;
  bhi = c - (c - bcy);
  blo = bcy - bhi;
  s0 = alo * blo - (s1 - ahi * bhi - alo * bhi - ahi * blo);
  t1 = acytail * bcx;
  c = kSplitter * acytail;
  ahi = c - (c - acytail);
  alo = acytail - ahi;
  c = kSplitter * bcx;
  bhi = c - (c - bcx);
  blo = bcx - bhi;
  t0 = alo * blo - (t1 - ahi * bhi - alo * bhi - ahi * blo);
  _i = s0 - t0;
  bvirt = s0 - _i;
  u[0] = s0 - (_i + bvirt) + (bvirt - t0);
  _j = s1 + _i;
  bvirt = _j - s1;
  _0 = s1 - (_j - bvirt) + (_i - bvirt);
  _i = _0 - t1;
  bvirt = _0 - _i;
  u[1] = _0 - (_i + bvirt) + (bvirt - t1);
  u3 = _j + _i;
  bvirt = u3 - _j;
  u[2] = _j - (u3 - bvirt) + (_i - bvirt);
  u[3] = u3;
  const int C1len = expansionSum(4, B, 4, u, C1);

  s1 = acx * bcytail;
  c = kSplitter * acx;
  ahi = c - (c - acx);
  alo = acx - ahi;
  c = kSplitter * bcytail;
  bhi = c - (c - bcytail);
  blo = bcytail - bhi;
  s0 = alo * blo - (s1 - ahi * bhi - alo * bhi - ahi * blo);
  t1 = acy * bcxtail;
  c = kSplitter * acy;
  ahi = c - (c - acy);
  alo = acy - ahi;
  c = kSplitter * bcxtail;
  bhi = c - (c - bcxtail);
  blo = bcxtail - bhi;
  t0 = alo * blo - (t1 - ahi * bhi - alo * bhi - ahi * blo);
  _i = s0 - t0;
  bvirt = s0 - _i;
  u[0] = s0 - (_i + bvirt) + (bvirt - t0);
  _j = s1 + _i;
  bvirt = _j - s1;
  _0 = s1 - (_j - bvirt) + (_i - bvirt);
  _i = _0 - t1;
  bvirt = _0 - _i;
  u[1] = _0 - (_i + bvirt) + (bvirt - t1);
  u3 = _j + _i;
  bvirt = u3 - _j;
  u[2] = _j - (u3 - bvirt) + (_i - bvirt);
  u[3] = u3;
  const int C2len = expansionSum(C1len, C1, 4, u, C2);

  s1 = acxtail * bcytail;
  c = kSplitter * acxtail;
  ahi = c - (c - acxtail);
  alo = acxtail - ahi;
  c = kSplitter * bcytail;
  bhi = c - (c - bcytail);
  blo = bcytail - bhi;
  s0 = alo * blo - (s1 - ahi * bhi - alo * bhi - ahi * blo);
  t1 = acytail * bcxtail;
  c = kSplitter * acytail;
  ahi = c - (c - acytail);
  alo = acytail - ahi;
  c = kSplitter * bcxtail;
  bhi = c - (c - bcxtail);
  blo = bcxtail - bhi;
  t0 = alo * blo - (t1 - ahi * bhi - alo * bhi - ahi * blo);
  _i = s0 - t0;
  bvirt = s0 - _i;
  u[0] = s0 - (_i + bvirt) + (bvirt - t0);
  _j = s1 + _i;
  bvirt = _j - s1;
  _0 = s1 - (_j - bvirt) + (_i - bvirt);
  _i = _0 - t1;
  bvirt = _0 - _i;
  u[1] = _0 - (_i + bvirt) + (bvirt - t1);
  u3 = _j + _i;
  bvirt = u3 - _j;
  u[2] = _j - (u3 - bvirt) + (_i - bvirt);
  u[3] = u3;
  const int Dlen = expansionSum(C2len, C2, 4, u, D);

  return D[Dlen - 1];
}

} // namespace

double orient2d(double ax, double ay, double bx, double by, double cx, double cy) {
  const double detleft = (ay - cy) * (bx - cx);
  const double detright = (ax - cx) * (by - cy);
  const double det = detleft - detright;

  if (detleft == 0 || detright == 0 || (detleft > 0) != (detright > 0)) return det;

  const double detsum = std::fabs(detleft + detright);
  if (std::fabs(det) >= kCcwerrboundA * detsum) return det;

  return -orient2dadapt(ax, ay, bx, by, cx, cy, detsum);
}

} // namespace polyclip
