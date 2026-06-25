#include "FitModel.h"

#include <cstdio>

namespace giggle {

namespace {

struct ShapeEntry
{
    ShapeKind kind;
    const char* name;
    std::vector<std::string> parameterNames;
};

// The single source of truth for shape names and their parameters.
const std::vector<ShapeEntry>& ShapeTable()
{
    static const std::vector<ShapeEntry> table = {
        { ShapeKind::Gaussian,     "gaussian",      { "mean", "sigma" } },
        { ShapeKind::GaussianTail, "gaussian_tail", { "mean", "sigma", "tail_fraction", "tail_length" } },
        { ShapeKind::Lorentzian,   "lorentzian",    { "mean", "gamma" } },
        { ShapeKind::Voigt,        "voigt",         { "mean", "sigma", "gamma" } },
        { ShapeKind::Constant,     "constant",      {} },
        { ShapeKind::Linear,       "linear",        { "slope" } },
        { ShapeKind::Quadratic,    "quadratic",     { "slope", "curvature" } },
        { ShapeKind::Exponential,  "exponential",   { "slope" } },
        { ShapeKind::Step,         "step",          { "edge", "width" } },
        { ShapeKind::Custom,       "custom",        {} },
    };
    return table;
}

} // namespace

const char* ShapeKindName(ShapeKind kind)
{
    for (const ShapeEntry& entry : ShapeTable())
    {
        if (entry.kind == kind)
        {
            return entry.name;
        }
    }
    return "unknown";
}

std::optional<ShapeKind> ShapeKindFromName(const std::string& name)
{
    for (const ShapeEntry& entry : ShapeTable())
    {
        if (name == entry.name)
        {
            return entry.kind;
        }
    }
    return std::nullopt;
}

std::vector<std::string> ShapeParameterNames(ShapeKind kind)
{
    for (const ShapeEntry& entry : ShapeTable())
    {
        if (entry.kind == kind)
        {
            return entry.parameterNames;
        }
    }
    return {};
}

const char* FitStatisticName(FitStatistic statistic)
{
    switch (statistic)
    {
        case FitStatistic::ChiSquare:         return "chi_square";
        case FitStatistic::PoissonLikelihood: return "poisson_likelihood";
    }
    return "unknown";
}

std::optional<FitStatistic> FitStatisticFromName(const std::string& name)
{
    if (name == "chi_square")
    {
        return FitStatistic::ChiSquare;
    }
    if (name == "poisson_likelihood")
    {
        return FitStatistic::PoissonLikelihood;
    }
    return std::nullopt;
}

const char* MinimizerAlgorithmName(MinimizerAlgorithm algorithm)
{
    switch (algorithm)
    {
        case MinimizerAlgorithm::Migrad:      return "migrad";
        case MinimizerAlgorithm::Simplex:     return "simplex";
        case MinimizerAlgorithm::Scan:        return "scan";
        case MinimizerAlgorithm::Combination: return "combination";
    }
    return "unknown";
}

std::optional<MinimizerAlgorithm> MinimizerAlgorithmFromName(const std::string& name)
{
    if (name == "migrad")
    {
        return MinimizerAlgorithm::Migrad;
    }
    if (name == "simplex")
    {
        return MinimizerAlgorithm::Simplex;
    }
    if (name == "scan")
    {
        return MinimizerAlgorithm::Scan;
    }
    if (name == "combination")
    {
        return MinimizerAlgorithm::Combination;
    }
    return std::nullopt;
}

const char* FitUncertaintiesName(FitUncertainties uncertainties)
{
    switch (uncertainties)
    {
        case FitUncertainties::Parabolic: return "parabolic";
        case FitUncertainties::Minos:     return "minos";
    }
    return "unknown";
}

std::optional<FitUncertainties> FitUncertaintiesFromName(const std::string& name)
{
    if (name == "parabolic")
    {
        return FitUncertainties::Parabolic;
    }
    if (name == "minos")
    {
        return FitUncertainties::Minos;
    }
    return std::nullopt;
}

std::string NextPeakLabel(const std::vector<FitComponent>& peaks)
{
    int highest = 0;
    for (const FitComponent& peak : peaks)
    {
        int number = 0;
        if (std::sscanf(peak.label.c_str(), "Peak %d", &number) == 1 && number > highest)
        {
            highest = number;
        }
    }
    return "Peak " + std::to_string(highest + 1);
}

} // namespace giggle
