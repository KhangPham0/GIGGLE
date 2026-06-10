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
    Lorentzian,
    Voigt,
    Constant,
    Linear,
    Quadratic,
    Exponential,
    Custom, // user-provided formula
};

// One additive component of the fit function.
//
// Every component is modeled as yield * (shape normalized over the fit
// range), so shape parameters never include an overall scale: the yield
// carries it, and equals the component's counts within the fit range.
struct FitComponent
{
    std::string label;   // user-visible, e.g. "Peak 1"
    ShapeKind shape = ShapeKind::Gaussian;
    std::string formula; // only used when shape == ShapeKind::Custom

    FitParameter yield;                   // counts in the fit range
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
    FitStatistic statistic = FitStatistic::PoissonLikelihood;
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

} // namespace giggle

#endif
