#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cmath>
#include <random>

#include "core/Shapes.h"
#include "rootbridge/FormulaSupport.h"
#include "rootbridge/RootFitEngine.h"

using namespace giggle;

// ---------------------------------------------------------------------------
// Toy spectra: a Gaussian peak on a falling linear background, generated
// without ROOT so the truth is fully under the test's control.
//
//   peak:       500 counts total, mean 50, sigma 3
//   background: density 40 - 0.2 x  (counts per x-unit)
//   histogram:  200 bins over [0, 100]; fit range [30, 70]
// ---------------------------------------------------------------------------

namespace {

constexpr double kPeakCounts = 500.0;
constexpr double kMean = 50.0;
constexpr double kSigma = 3.0;
constexpr double kFitLo = 30.0;
constexpr double kFitHi = 70.0;

double GaussianBinFraction(double a, double b)
{
    double parameters[2] = { kMean, kSigma };
    double total = ShapeIntegral(ShapeKind::Gaussian, parameters, 2, -1000.0, 1000.0, 0.0);
    return ShapeIntegral(ShapeKind::Gaussian, parameters, 2, a, b, 0.0) / total;
}

// The true peak counts inside the fit range (the in-range convention).
double TruePeakYield()
{
    return kPeakCounts * GaussianBinFraction(kFitLo, kFitHi);
}

HistogramData GenerateToy(unsigned int seed)
{
    HistogramData data;
    data.name = "toy";
    const int binCount = 200;
    for (int i = 0; i <= binCount; ++i)
    {
        data.binEdges.push_back(100.0 * i / binCount);
    }

    std::mt19937 rng(seed);
    for (int i = 0; i < binCount; ++i)
    {
        double a = data.binEdges[i];
        double b = data.binEdges[i + 1];
        double expected = kPeakCounts * GaussianBinFraction(a, b)
                          + (40.0 * (b - a) - 0.1 * (b * b - a * a));
        std::poisson_distribution<int> poisson(expected);
        data.counts.push_back(poisson(rng));
    }
    return data;
}

FitModel MakeToyModel()
{
    FitModel model;
    model.range = { kFitLo, kFitHi };
    model.statistic = FitStatistic::PoissonLikelihood;

    // True peak amplitude: 500 / (3 sqrt(2 pi)) ~ 66.5 counts per x-unit.
    FitComponent peak;
    peak.label = "Peak 1";
    peak.shape = ShapeKind::Gaussian;
    peak.amplitude = { "amplitude", 55.0, false, 0.0, std::nullopt };
    peak.parameters = {
        { "mean", 49.0, false, std::nullopt, std::nullopt },
        { "sigma", 3.5, false, std::nullopt, std::nullopt },
    };
    model.peaks.push_back(peak);

    // True background at the pivot (x = 50): density 30, relative slope
    // -0.2 / 30 ~ -0.00667 per x-unit.
    FitComponent background;
    background.label = "Background";
    background.shape = ShapeKind::Linear;
    background.amplitude = { "amplitude", 25.0, false, 0.0, std::nullopt };
    background.parameters = {
        { "slope", -0.003, false, std::nullopt, std::nullopt },
    };
    model.background.push_back(background);

    return model;
}

} // namespace

TEST_CASE("the engine recovers the truth on one toy spectrum")
{
    RootFitEngine engine;
    HistogramData data = GenerateToy(1);
    FitModel model = MakeToyModel();

    FitResult result = engine.Fit(data, model);

    REQUIRE(result.converged);
    REQUIRE(result.peaks.size() == 1);
    REQUIRE(result.background.size() == 1);

    const ComponentResult& peak = result.peaks[0];
    CHECK(peak.counts.error > 0.0);
    CHECK(std::abs(peak.counts.value - TruePeakYield()) < 5.0 * peak.counts.error);
    CHECK(peak.parameters[0].value == doctest::Approx(kMean).epsilon(0.01));
    CHECK(peak.parameters[1].value == doctest::Approx(kSigma).epsilon(0.15));

    // 80 bins in range, 5 free parameters.
    CHECK(result.degreesOfFreedom == 75);

    // The covariance is square in the engine's parameter order and carries
    // the amplitude variance on the diagonal.
    REQUIRE(result.covariance.size() == 5);
    CHECK(std::sqrt(result.covariance[0][0]) == doctest::Approx(peak.amplitude.error).epsilon(1e-6));

    // Curves are in data units (counts per bin) across the range.
    REQUIRE(!result.curves.x.empty());
    CHECK(result.curves.x.front() == doctest::Approx(kFitLo));
    CHECK(result.curves.x.back() == doctest::Approx(kFitHi));
}

TEST_CASE("counts and uncertainties are calibrated over many toys")
{
    RootFitEngine engine;
    FitModel model = MakeToyModel();
    double truth = TruePeakYield();

    std::vector<double> pulls;
    double countsSum = 0.0;

    const int toyCount = 150;
    for (int toy = 0; toy < toyCount; ++toy)
    {
        FitResult result = engine.Fit(GenerateToy(100 + toy), model);
        REQUIRE(result.converged);
        const ValueWithError& counts = result.peaks[0].counts;
        pulls.push_back((counts.value - truth) / counts.error);
        countsSum += counts.value;
    }

    double pullMean = 0.0;
    for (double pull : pulls)
    {
        pullMean += pull;
    }
    pullMean /= pulls.size();

    double pullVariance = 0.0;
    for (double pull : pulls)
    {
        pullVariance += (pull - pullMean) * (pull - pullMean);
    }
    double pullWidth = std::sqrt(pullVariance / (pulls.size() - 1));

    // Unbiased counts and a correctly sized error bar: the whole point.
    CHECK(std::abs(countsSum / toyCount - truth) / truth < 0.02);
    CHECK(std::abs(pullMean) < 0.2);
    CHECK(pullWidth > 0.8);
    CHECK(pullWidth < 1.2);
}

TEST_CASE("every converged fit passes the TF1NormSum cross-check")
{
    RootFitEngine engine;

    for (unsigned int seed : { 7u, 8u, 9u })
    {
        FitModel model = MakeToyModel();
        FitResult likelihood = engine.Fit(GenerateToy(seed), model);
        REQUIRE(likelihood.converged);
        CHECK(likelihood.normSumCheck.performed);
        CHECK(likelihood.normSumCheck.agreed);

        model.statistic = FitStatistic::ChiSquare;
        FitResult chiSquare = engine.Fit(GenerateToy(seed), model);
        REQUIRE(chiSquare.converged);
        CHECK(chiSquare.normSumCheck.performed);
        CHECK(chiSquare.normSumCheck.agreed);
    }
}

TEST_CASE("mid-bin ranges fit identically to their snapped versions")
{
    RootFitEngine engine;
    HistogramData data = GenerateToy(17);

    FitModel snapped = MakeToyModel(); // [30, 70], on edges (bin width 0.5)
    FitModel midBin = MakeToyModel();
    midBin.range = { 30.2, 69.8 }; // snaps to [30, 70]

    FitResult a = engine.Fit(data, snapped);
    FitResult b = engine.Fit(data, midBin);

    REQUIRE(a.converged);
    REQUIRE(b.converged);
    CHECK(a.peaks[0].counts.value == doctest::Approx(b.peaks[0].counts.value));
    CHECK(a.peaks[0].counts.error == doctest::Approx(b.peaks[0].counts.error));
    CHECK(a.degreesOfFreedom == b.degreesOfFreedom);
}

TEST_CASE("the model total matches the raw counts in range")
{
    RootFitEngine engine;
    HistogramData data = GenerateToy(19);
    FitModel model = MakeToyModel();

    FitResult result = engine.Fit(data, model);
    REQUIRE(result.converged);

    ValueWithError raw = CountsInRange(data, model.range);
    CHECK(result.totalCounts.value == doctest::Approx(raw.value).epsilon(0.05));
    CHECK(result.totalCounts.error > 0.0);
    // The total is the sum of the component counts.
    CHECK(result.totalCounts.value
          == doctest::Approx(result.peaks[0].counts.value + result.background[0].counts.value));
}

TEST_CASE("fixed parameters stay fixed, including the amplitude")
{
    RootFitEngine engine;
    HistogramData data = GenerateToy(11);

    FitModel model = MakeToyModel();
    model.peaks[0].parameters[1].value = 3.0; // sigma
    model.peaks[0].parameters[1].fixed = true;

    FitResult result = engine.Fit(data, model);
    REQUIRE(result.converged);
    CHECK(result.peaks[0].parameters[1].value == 3.0);
    CHECK(result.peaks[0].parameters[1].error == 0.0);
    CHECK(result.peaks[0].counts.value >= 0.0);
    CHECK(result.degreesOfFreedom == 76); // one parameter fewer to fit
    // The cross-check mirrors fixed shape parameters.
    CHECK(result.normSumCheck.performed);
    CHECK(result.normSumCheck.agreed);

    // Fixing the amplitude itself: the fit honors it; the cross-check is
    // skipped (TF1NormSum has no equivalent constraint).
    FitModel fixedAmplitude = MakeToyModel();
    fixedAmplitude.peaks[0].amplitude.value = 60.0;
    fixedAmplitude.peaks[0].amplitude.fixed = true;
    FitResult fixedResult = engine.Fit(data, fixedAmplitude);
    REQUIRE(fixedResult.converged);
    CHECK(fixedResult.peaks[0].amplitude.value == 60.0);
    CHECK(fixedResult.peaks[0].amplitude.error == 0.0);
    CHECK(!fixedResult.normSumCheck.performed);
}

TEST_CASE("the result warns when a peak leaves the range or a parameter hits a bound")
{
    RootFitEngine engine;
    HistogramData data = GenerateToy(23);

    // A clean central peak: no warnings.
    FitResult clean = engine.Fit(data, MakeToyModel());
    REQUIRE(clean.converged);
    CHECK(clean.warnings.empty());

    // The fit window cut down to [44, 56]: the peak's 3 sigma extends past
    // both edges. Sigma is fixed to keep this narrow-window fit stable.
    FitModel clipped = MakeToyModel();
    clipped.range = { 44.0, 56.0 };
    clipped.peaks[0].parameters[1].value = 3.0;
    clipped.peaks[0].parameters[1].fixed = true;
    FitResult clippedResult = engine.Fit(data, clipped);
    REQUIRE(clippedResult.converged);
    REQUIRE(!clippedResult.warnings.empty());
    CHECK(clippedResult.warnings[0].find("extends past the fit range") != std::string::npos);

    // An amplitude forced onto its bound: the true amplitude is ~66, so a
    // lower bound of 90 pins the parameter there.
    FitModel pinned = MakeToyModel();
    pinned.peaks[0].amplitude.value = 95.0;
    pinned.peaks[0].amplitude.lowerBound = 90.0;
    FitResult pinnedResult = engine.Fit(data, pinned);
    if (pinnedResult.converged)
    {
        bool foundBoundWarning = false;
        for (const std::string& warning : pinnedResult.warnings)
        {
            if (warning.find("at its bound") != std::string::npos)
            {
                foundBoundWarning = true;
            }
        }
        CHECK(foundBoundWarning);
    }
}

TEST_CASE("a tailed peak fits end to end with calibrated counts")
{
    // A toy generated from a tailed gaussian truth, built with the same
    // core math the engine uses.
    FitComponent truth;
    truth.shape = ShapeKind::GaussianTail;
    truth.amplitude = { "amplitude", 66.0, false, std::nullopt, std::nullopt };
    truth.parameters = {
        { "mean", 50.0, false, std::nullopt, std::nullopt },
        { "sigma", 3.0, false, std::nullopt, std::nullopt },
        { "tail_fraction", 0.3, false, std::nullopt, std::nullopt },
        { "tail_length", 2.5, false, std::nullopt, std::nullopt },
    };
    FitRange fitRange{ kFitLo, kFitHi };
    double truthCounts = ComponentCounts(truth, fitRange);

    HistogramData data;
    data.name = "tailed_toy";
    const int binCount = 200;
    for (int i = 0; i <= binCount; ++i)
    {
        data.binEdges.push_back(100.0 * i / binCount);
    }
    std::mt19937 rng(31);
    for (int i = 0; i < binCount; ++i)
    {
        double expected = truth.amplitude.value
                              * ShapeIntegral(truth, fitRange, data.binEdges[i], data.binEdges[i + 1])
                          + 15.0 * (data.binEdges[i + 1] - data.binEdges[i]);
        std::poisson_distribution<int> poisson(expected);
        data.counts.push_back(poisson(rng));
    }

    FitModel model;
    model.range = fitRange;
    model.statistic = FitStatistic::PoissonLikelihood;

    // Tail parameters are fixed, as in real gf3-style analyses: fitted
    // freely on modest statistics they are degenerate with sigma (a short
    // tail mimics a wider gaussian), and the cross-check rightly balks at
    // such collapsed fits.
    FitComponent peak = truth;
    peak.label = "Peak 1";
    peak.amplitude = { "amplitude", 50.0, false, 0.0, std::nullopt };
    peak.parameters[0].value = 49.0; // mean
    peak.parameters[1].value = 3.5;  // sigma
    peak.parameters[2] = { "tail_fraction", 0.3, true, std::nullopt, std::nullopt };
    peak.parameters[3] = { "tail_length", 2.5, true, std::nullopt, std::nullopt };
    model.peaks.push_back(peak);

    FitComponent background;
    background.label = "Background";
    background.shape = ShapeKind::Constant;
    background.amplitude = { "amplitude", 12.0, false, 0.0, std::nullopt };
    model.background.push_back(background);

    RootFitEngine engine;
    FitResult result = engine.Fit(data, model);

    REQUIRE(result.converged);
    CHECK(std::abs(result.peaks[0].counts.value - truthCounts)
          < 5.0 * result.peaks[0].counts.error);
    CHECK(result.normSumCheck.performed);
    CHECK(result.normSumCheck.agreed);
}

TEST_CASE("bad custom formulas and empty models fail gracefully")
{
    RootFitEngine engine;
    HistogramData data = GenerateToy(13);

    FitModel empty;
    empty.range = { kFitLo, kFitHi };
    CHECK(!engine.Fit(data, empty).converged);

    // A custom shape with an empty formula is rejected with a clear message.
    FitModel custom = MakeToyModel();
    custom.peaks[0].shape = ShapeKind::Custom;
    custom.peaks[0].formula.clear();
    FitResult result = engine.Fit(data, custom);
    CHECK(!result.converged);
    CHECK(result.message.find("empty") != std::string::npos);
}

TEST_CASE("the formula validator rejects scales and accepts honest shapes")
{
    FormulaCheckResult good = ValidateFormula("1.0/(1.0+exp((x-[0])/[1]))");
    CHECK(good.valid);
    CHECK(good.parameterCount == 2);

    // [0] only scales the formula: degenerate with the amplitude.
    FormulaCheckResult scaled = ValidateFormula("[0]*exp(-0.5*((x-[1])/[2])^2)");
    CHECK(!scaled.valid);
    CHECK(scaled.message.find("[0]") != std::string::npos);

    CHECK(!ValidateFormula("not a formula ((").valid);
    CHECK(!ValidateFormula("").valid);
    CHECK(!ValidateFormula("0*x").valid); // identically zero
}

TEST_CASE("voigt peaks and custom backgrounds fit end to end")
{
    InstallCustomShapeEvaluator();
    RootFitEngine engine;
    HistogramData data = GenerateToy(29);

    // The Gaussian toy fitted with a Voigt: gamma should come out small
    // and the counts should still match the truth.
    FitModel voigtModel = MakeToyModel();
    voigtModel.peaks[0].shape = ShapeKind::Voigt;
    voigtModel.peaks[0].parameters.push_back({ "gamma", 0.3, false, 0.0, std::nullopt });
    FitResult voigtResult = engine.Fit(data, voigtModel);
    REQUIRE(voigtResult.converged);
    CHECK(std::abs(voigtResult.peaks[0].counts.value - TruePeakYield())
          < 5.0 * voigtResult.peaks[0].counts.error);
    CHECK(voigtResult.normSumCheck.performed);
    CHECK(voigtResult.normSumCheck.agreed);

    // The linear background swapped for an equivalent custom formula
    // (no overall scale: the amplitude provides it).
    FitModel customModel = MakeToyModel();
    customModel.background[0].shape = ShapeKind::Custom;
    customModel.background[0].formula = "1.0+[0]*(x-50.0)";
    customModel.background[0].parameters = {
        { "p0", -0.003, false, std::nullopt, std::nullopt },
    };
    FitResult customResult = engine.Fit(data, customModel);
    REQUIRE(customResult.converged);
    CHECK(std::abs(customResult.peaks[0].counts.value - TruePeakYield())
          < 5.0 * customResult.peaks[0].counts.error);
    CHECK(customResult.normSumCheck.performed);
    CHECK(customResult.normSumCheck.agreed);
}
