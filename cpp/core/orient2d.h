#pragma once

namespace polyclip {

/* Robust orientation predicate, ported from the `robust-predicates` npm
 * package (ISC license, (c) 2019 Volodymyr Agafonkin), itself a port of
 * Jonathan Shewchuk's public-domain adaptive-precision predicates.
 *
 * Returns a value whose sign matches the JS robust-predicates orient2d:
 * negative if a, b, c occur in counterclockwise order, positive if
 * clockwise, zero if exactly collinear.
 *
 * Must be compiled with FP contraction disabled (-ffp-contract=off) so the
 * error-free transformations keep exact IEEE-754 double semantics. */
double orient2d(double ax, double ay, double bx, double by, double cx, double cy);

} // namespace polyclip
