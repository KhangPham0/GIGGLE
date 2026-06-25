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

// The Minuit2 minimization algorithm. MIGRAD is the standard; the others
// are slower fallbacks for fits that will not converge.
enum class MinimizerAlgorithm
{
    Migrad,
    Simplex,
    Scan,
    Combination,
};

// How much Minuit2 prints to the terminal during the fit. It does not
// affect the result, so it is a session preference and is not serialized.
enum class PrintLevel
{
    Quiet,
    Normal,
    Verbose,
};

// How parameter uncertainties are computed. Parabolic is the fast HESSE
// error; MINOS profiles each parameter for asymmetric errors.
enum class FitUncertainties
{
    Parabolic,
    Minos,
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

    // How the fit is run. All of these shape the result and travel with a
    // preset, except printLevel, which is session-only terminal verbosity.
    MinimizerAlgorithm algorithm = MinimizerAlgorithm::Migrad;
    FitUncertainties uncertainties = FitUncertainties::Parabolic;
    bool integrateBins = false;   // compare a bin to the function's integral,
                                  // not its value at the bin center
    bool ignoreBinErrors = false; // treat every bin as weight 1 (unweighted)
    bool countEmptyBins = false;  // include zero-count bins (always on for likelihood)
    double tolerance = 0.0;       // 0 = Minuit2 default
    int maxIterations = 0;        // 0 = Minuit2 default
    PrintLevel printLevel = PrintLevel::Quiet;

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

const char* MinimizerAlgorithmName(MinimizerAlgorithm algorithm);
std::optional<MinimizerAlgorithm> MinimizerAlgorithmFromName(const std::string& name);

const char* FitUncertaintiesName(FitUncertainties uncertainties);
std::optional<FitUncertainties> FitUncertaintiesFromName(const std::string& name);

// "Peak <n>" with n above every number already used by the given peaks.
std::string NextPeakLabel(const std::vector<FitComponent>& peaks);

} // namespace giggle

#endif
