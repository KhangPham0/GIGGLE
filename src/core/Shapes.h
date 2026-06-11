#ifndef GIGGLE_CORE_SHAPES_H
#define GIGGLE_CORE_SHAPES_H

#include "FitModel.h"
#include "FitResult.h"

namespace giggle {

// Shape evaluation for drawing model curves and, eventually, for checking
// the fit engine against an independent implementation.
//
// These formulas define what each ShapeKind means. The fit engine must
// implement exactly the same functions. Parameters are taken from the
// component in ShapeParameterNames order.
//
//   gaussian     exp(-(x - mean)^2 / (2 sigma^2))
//   lorentzian   1 / (1 + ((x - mean) / gamma)^2)
//   constant     1
//   linear       1 + slope * x
//   quadratic    1 + slope * x + curvature * x^2
//   exponential  exp(slope * x)
//
// Voigt and custom shapes are not implemented here yet; they evaluate to 0
// (no preview curve until they are).

// The unnormalized shape value at x.
double ShapeValue(const FitComponent& component, double x);

// The integral of the unnormalized shape over [a, b], in closed form.
double ShapeIntegral(const FitComponent& component, double a, double b);

// The full component value at x:
//
//   yield * shape(x) / integral of shape over the fit range
//
// so the component's own integral over the range equals its yield (the
// in-range count convention). Returns 0 when the shape cannot be
// normalized (unimplemented shape, or an integral that is zero or
// negative).
double ComponentValue(const FitComponent& component, const FitRange& range, double x);

// Samples every component and their sum across the fit range, for drawing.
// Curves are ordered peaks first, then background, as in the model.
FitCurves SampleModelCurves(const FitModel& model, int pointCount);

} // namespace giggle

#endif
