#ifndef GIGGLE_CORE_FIT_MODEL_H
#define GIGGLE_CORE_FIT_MODEL_H

#include <optional>
#include <string>
#include <vector>

namespace giggle {

// One scalar parameter of a fit component: a value (initial guess before the
// fit), an optional fixed flag, and optional bounds.
struct FitParameter
{
    std::string name;
    double value = 0.0;
    bool fixed = false;
    std::optional<double> lowerBound;
    std::optional<double> upperBound;
};

// The function shapes a component can take. Peaks and backgrounds share one
// list; which shapes the UI offers in each section is the UI's decision.
enum class ShapeKind
{
    Gaussian,
    GaussianTail, // gaussian + low-energy exponential tail (gf3/Hypermet style)
    Lorentzian,
    Voigt,
    Constant,
    Linear,
    Quadratic,
    Exponential,
    Step, // smoothed step background (erfc), high on the low-energy side
    Custom, // user-provided formula
};

// One additive component of the fit function.
//
// Every component is modeled as amplitude * shape, where shapes are
// defined to be 1 at their reference point (a peak's mean; the fit range
// center for backgrounds). The amplitude is therefore the component's
// density at that point, and amplitude * bin width is the height read off
// the plot -- a constant conversion, so fixing or bounding the height is
// exactly fixing or bounding this parameter. The component's counts within
// the fit range are computed from the fit afterwards, with the uncertainty
// propagated through the covariance.
struct FitComponent
{
    std::string label;   // user-visible, e.g. "Peak 1"
    ShapeKind shape = ShapeKind::Gaussian;
    std::string formula; // only used when shape == ShapeKind::Custom

    FitParameter amplitude;               // density at the reference point
    std::vector<FitParameter> parameters; // the shape's parameters, in order
};

enum class FitStatistic
{
    ChiSquare,
    PoissonLikelihood,
};

struct FitRange
{
    double min = 0.0;
    double max = 0.0;
};

// The complete description of one fit: what to fit and how.
struct FitModel
{
    FitRange range;
    // Chi-square by default: it matches ROOT's convention, and it keeps
    // the data-vs-model-total comparison informative (a Poisson likelihood
    // fit reproduces the data total by construction). Likelihood is the
    // better choice for low-count spectra.
    FitStatistic statistic = FitStatistic::ChiSquare;
    std::vector<FitComponent> peaks;
    std::vector<FitComponent> background;
};

// Shape lookup helpers, backed by one table in FitModel.cpp.
const char* ShapeKindName(ShapeKind kind);
std::optional<ShapeKind> ShapeKindFromName(const std::string& name);

// The parameter names a shape expects, in order. Custom shapes return an
// empty list: their parameters are defined by the user's formula.
std::vector<std::string> ShapeParameterNames(ShapeKind kind);

const char* FitStatisticName(FitStatistic statistic);
std::optional<FitStatistic> FitStatisticFromName(const std::string& name);

// "Peak <n>" with n above every number already used by the given peaks.
std::string NextPeakLabel(const std::vector<FitComponent>& peaks);

} // namespace giggle

#endif
