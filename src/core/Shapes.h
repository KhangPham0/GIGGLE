#ifndef GIGGLE_CORE_SHAPES_H
#define GIGGLE_CORE_SHAPES_H

#include <optional>
#include <vector>

#include "FitModel.h"
#include "FitResult.h"
#include "HistogramData.h"

namespace giggle {

// Shape evaluation for drawing model curves and for the count extraction.
//
// These formulas define what each ShapeKind means. The fit engine must
// implement exactly the same functions. Parameters are taken from the
// component in ShapeParameterNames order. `pivot` is the reference point
// of the background shapes (the fit range center); peak shapes ignore it.
//
// Every shape is 1 at its reference point, so amplitude * shape is a
// density whose value there is the amplitude itself:
//
//   gaussian     exp(-(x - mean)^2 / (2 sigma^2))           = 1 at mean
//   lorentzian   1 / (1 + ((x - mean) / gamma)^2)           = 1 at mean
//   constant     1
//   linear       1 + slope * (x - pivot)                    = 1 at pivot
//   quadratic    1 + slope * (x - pivot)
//                  + curvature * (x - pivot)^2              = 1 at pivot
//   exponential  exp(slope * (x - pivot))                   = 1 at pivot
//
// Voigt and custom shapes are not implemented here yet; they evaluate to 0
// (no preview curve until they are).

// True when ShapeValue/ShapeIntegral can evaluate this kind.
bool ShapeIsImplemented(ShapeKind kind);

// The unit-amplitude shape value at x. The raw-array forms exist so the
// fit engine can evaluate shapes without building FitComponent objects;
// both forms are the same math.
double ShapeValue(ShapeKind kind, const double* parameters, int parameterCount, double x, double pivot);
double ShapeValue(const FitComponent& component, const FitRange& range, double x);

// The integral of the unit-amplitude shape over [a, b], in closed form.
double ShapeIntegral(ShapeKind kind, const double* parameters, int parameterCount,
                     double a, double b, double pivot);
double ShapeIntegral(const FitComponent& component, const FitRange& range, double a, double b);

// The reference point shapes are anchored to: the fit range center.
inline double RangePivot(const FitRange& range)
{
    return (range.min + range.max) / 2.0;
}

// The component's density at x: amplitude * shape.
double ComponentDensity(const FitComponent& component, const FitRange& range, double x);

// The component's counts within the fit range (in-range convention):
//
//   N = amplitude * integral of shape over the range
//
// The gradient is with respect to [amplitude, shape parameters...] (shape
// derivatives by central finite differences); it feeds the covariance
// propagation of the count uncertainty.
double ComponentCounts(const FitComponent& component, const FitRange& range);
std::vector<double> ComponentCountsGradient(const FitComponent& component, const FitRange& range);

// Counts with the uncertainty from the component's covariance block, in
// [amplitude, shape parameters...] order: sigma^2 = g . C . g.
ValueWithError ComponentCountsWithError(const FitComponent& component, const FitRange& range,
                                        const std::vector<std::vector<double>>& covarianceBlock);

// Derived peak properties with linearly propagated errors. Empty when the
// shape has no such property (backgrounds; shapes not yet implemented).
std::optional<ValueWithError> PeakCentroid(ShapeKind kind, const ComponentResult& result);
std::optional<ValueWithError> PeakFWHM(ShapeKind kind, const ComponentResult& result);

// The width of the bin containing x (the width of the nearest bin when x
// is outside the histogram).
double BinWidthAt(const HistogramData& histogram, double x);

// Moves both range ends to the nearest bin edges (keeping at least one
// bin). The fit selects whole bins, so the range a fit actually uses --
// for data selection and for the count extraction alike -- must sit on
// bin edges; a range typed mid-bin would make "counts in range" ambiguous.
FitRange SnapRangeToBinEdges(const HistogramData& histogram, FitRange range);

// The raw data counts in the bins whose centers lie inside the range (the
// same bin selection the fit uses), with the plain counting uncertainty.
// Comparing this to the fitted total shows whether the fit accounts for
// the data.
ValueWithError CountsInRange(const HistogramData& histogram, const FitRange& range);

// A Gaussian peak guessed from the data around x: the mean from the
// tallest nearby bin, the height from its contents above the local
// baseline, and sigma from a half-maximum scan. Used by click-to-add and
// by the Add Peak button.
FitComponent SuggestGaussianPeak(const HistogramData& histogram, const FitRange& range, double x);

// Samples every component and their sum across the fit range, for drawing.
// Curves are ordered peaks first, then background, as in the model.
//
// ComponentDensity is a density (counts per x-unit), but histogram bins
// hold counts: the expected content of a bin is density * bin width. When
// a histogram is given, curves are scaled by the local bin width so they
// overlay the data in the same units; without one they stay densities.
FitCurves SampleModelCurves(const FitModel& model, int pointCount,
                            const HistogramData* histogramForUnits = nullptr);

} // namespace giggle

#endif
