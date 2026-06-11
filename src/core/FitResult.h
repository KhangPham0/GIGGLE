#ifndef GIGGLE_CORE_FIT_RESULT_H
#define GIGGLE_CORE_FIT_RESULT_H

#include <string>
#include <vector>

namespace giggle {

struct ValueWithError
{
    double value = 0.0;
    double error = 0.0;
};

// The fitted state of one component. Parameters are in the same order as in
// the FitModel component it corresponds to.
struct ComponentResult
{
    std::string label;
    ValueWithError counts;    // counts in the fit range, derived from the
                              // fit with the covariance-propagated error
    ValueWithError amplitude; // the fitted amplitude parameter
    std::vector<ValueWithError> parameters;
};

// The outcome of re-fitting the same data through ROOT's TF1NormSum and
// comparing its in-range counts against ours. Runs on every converged fit.
struct CrossCheck
{
    bool performed = false; // false: see detail for why it was skipped
    bool agreed = false;
    std::string detail;
};

// Curves sampled by the fit engine for drawing. Not serialized.
struct FitCurves
{
    std::vector<double> x;
    std::vector<double> total;
    // One curve per component: peaks first, then background, in model order.
    std::vector<std::vector<double>> components;
};

// Everything a fit produces.
struct FitResult
{
    bool converged = false;
    std::string message; // human-readable status from the engine

    double chiSquare = 0.0;
    int degreesOfFreedom = 0;

    std::vector<ComponentResult> peaks;
    std::vector<ComponentResult> background;

    // The model's total counts in the fit range, with the error from the
    // full covariance (correlations included). Directly comparable to the
    // raw data counts in the range.
    ValueWithError totalCounts;

    CrossCheck normSumCheck;

    // Covariance matrix of the parameters, in the engine's parameter
    // order: for each component (peaks first, then background, in model
    // order), the amplitude followed by that component's shape parameters.
    std::vector<std::vector<double>> covariance;

    FitCurves curves;
};

struct FitModel;

// Writes the fitted values back into the model, so the panel and the
// preview curves show the fit. Errors stay in the result.
void ApplyFitResult(const FitResult& result, FitModel& model);

} // namespace giggle

#endif
