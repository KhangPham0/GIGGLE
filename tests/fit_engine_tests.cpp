#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cmath>
#include <random>

#include "TF1.h"
#include "TMath.h"
#include "Math/PdfFuncMathCore.h"

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

TEST_CASE("a fit pinned on a shape bound still passes the cross-check")
{
    // The bound is mirrored into the TF1NormSum re-fit, so a result that
    // legitimately sits on it is compared under the same rules.
    RootFitEngine engine;
    HistogramData data = GenerateToy(37);

    FitModel model = MakeToyModel();
    // The true sigma is 3.0; this floor forces the fit onto the bound.
    model.peaks[0].parameters[1].value = 3.6;
    model.peaks[0].parameters[1].lowerBound = 3.5;
    model.peaks[0].parameters[1].upperBound = 4.5;

    FitResult result = engine.Fit(data, model);
    REQUIRE(result.converged);
    CHECK(result.peaks[0].parameters[1].value == doctest::Approx(3.5)); // pinned
    bool boundWarning = false;
    for (const std::string& warning : result.warnings)
    {
        boundWarning |= warning.find("at its bound") != std::string::npos;
    }
    CHECK(boundWarning);
    CHECK(result.normSumCheck.performed);
    CHECK(result.normSumCheck.agreed);
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

TEST_CASE("the integral bin option removes the midpoint bias on an under-sampled peak")
{
    // A peak about half a bin wide: the midpoint rule misjudges the steep
    // central bins, the true bin integral does not. The data is noise-free
    // (the exact bin integrals), so any difference is the bin model, not
    // statistics. This also confirms ROOT's fIntegral normalization matches
    // our density * bin-width function.
    const double mean = 50.0;
    const double sigma = 1.0;
    const double ampTruth = 100.0; // counts per x-unit at the peak
    const double bgDensity = 5.0;

    FitComponent truthPeak;
    truthPeak.shape = ShapeKind::Gaussian;
    truthPeak.amplitude = { "amplitude", ampTruth, false, std::nullopt, std::nullopt };
    truthPeak.parameters = {
        { "mean", mean, false, std::nullopt, std::nullopt },
        { "sigma", sigma, false, std::nullopt, std::nullopt },
    };
    FitRange range{ 40.0, 60.0 };
    double truthCounts = ComponentCounts(truthPeak, range);

    HistogramData data;
    data.name = "coarse";
    const int binCount = 20; // bin width 2.0 over [30, 70]
    for (int i = 0; i <= binCount; ++i)
    {
        data.binEdges.push_back(30.0 + 40.0 * i / binCount);
    }
    for (int i = 0; i < binCount; ++i)
    {
        double a = data.binEdges[i];
        double b = data.binEdges[i + 1];
        data.counts.push_back(ampTruth * ShapeIntegral(truthPeak, range, a, b)
                              + bgDensity * (b - a));
    }

    FitModel model;
    model.range = range;
    model.statistic = FitStatistic::ChiSquare;
    FitComponent peak;
    peak.label = "Peak 1";
    peak.shape = ShapeKind::Gaussian;
    peak.amplitude = { "amplitude", 80.0, false, 0.0, std::nullopt };
    peak.parameters = {
        { "mean", mean, true, std::nullopt, std::nullopt },  // fixed to truth
        { "sigma", sigma, true, std::nullopt, std::nullopt }, // fixed to truth
    };
    model.peaks.push_back(peak);
    FitComponent background;
    background.label = "Background";
    background.shape = ShapeKind::Constant;
    background.amplitude = { "amplitude", 4.0, false, 0.0, std::nullopt };
    model.background.push_back(background);

    RootFitEngine engine;

    FitModel midpoint = model;
    midpoint.integrateBins = false;
    FitResult midpointResult = engine.Fit(data, midpoint);

    FitModel integral = model;
    integral.integrateBins = true;
    FitResult integralResult = engine.Fit(data, integral);

    REQUIRE(midpointResult.converged);
    REQUIRE(integralResult.converged);

    double integralErr = std::abs(integralResult.peaks[0].counts.value - truthCounts) / truthCounts;
    double midpointErr = std::abs(midpointResult.peaks[0].counts.value - truthCounts) / truthCounts;

    // The integral recovers the truth; the midpoint is visibly biased; and
    // the two disagree -- so the option does something real.
    CHECK(integralErr < 0.02);
    CHECK(midpointErr > integralErr);
    CHECK(std::abs(midpointResult.peaks[0].counts.value - integralResult.peaks[0].counts.value)
          > 0.01 * truthCounts);
}

TEST_CASE("alternative minimizer algorithms reach the same minimum")
{
    RootFitEngine engine;
    HistogramData data = GenerateToy(5);
    double truth = TruePeakYield();

    for (MinimizerAlgorithm algorithm : { MinimizerAlgorithm::Simplex,
                                          MinimizerAlgorithm::Combination })
    {
        FitModel model = MakeToyModel();
        model.algorithm = algorithm;
        FitResult result = engine.Fit(data, model);
        REQUIRE(result.converged);
        CHECK(std::abs(result.peaks[0].counts.value - truth)
              < 5.0 * result.peaks[0].counts.error);
        CHECK(result.normSumCheck.agreed);
    }
}

TEST_CASE("ignoring bin errors still produces a sensible fit")
{
    RootFitEngine engine;
    HistogramData data = GenerateToy(15);

    FitModel model = MakeToyModel();
    model.statistic = FitStatistic::ChiSquare;
    model.ignoreBinErrors = true;
    FitResult result = engine.Fit(data, model);

    REQUIRE(result.converged);
    CHECK(result.peaks[0].counts.value > 0.0);
    CHECK(std::abs(result.peaks[0].counts.value - TruePeakYield())
          < 6.0 * result.peaks[0].counts.error);
}

TEST_CASE("MINOS gives asymmetric parameter and count errors")
{
    RootFitEngine engine;
    HistogramData data = GenerateToy(3);

    FitModel parabolic = MakeToyModel();
    FitResult parabolicResult = engine.Fit(data, parabolic);
    REQUIRE(parabolicResult.converged);
    // Parabolic mode leaves the asymmetric errors unset.
    CHECK(!parabolicResult.peaks[0].amplitude.asymmetric());
    CHECK(!parabolicResult.peaks[0].counts.asymmetric());

    FitModel minos = MakeToyModel();
    minos.uncertainties = FitUncertainties::Minos;
    FitResult minosResult = engine.Fit(data, minos);
    REQUIRE(minosResult.converged);

    // Free parameters carry both MINOS magnitudes, and for a well-behaved
    // Gaussian they sit close to the symmetric HESSE error.
    const ValueWithError& amplitude = minosResult.peaks[0].amplitude;
    REQUIRE(amplitude.asymmetric());
    CHECK(amplitude.errorLow.value() > 0.0);
    CHECK(amplitude.errorHigh.value() > 0.0);
    CHECK(amplitude.errorHigh.value() == doctest::Approx(amplitude.error).epsilon(0.5));

    CHECK(minosResult.peaks[0].parameters[0].asymmetric()); // mean

    // The counts become asymmetric via the TF1NormSum cross-check (its
    // coefficients are the counts), once the two fits agree.
    REQUIRE(minosResult.normSumCheck.agreed);
    const ValueWithError& counts = minosResult.peaks[0].counts;
    REQUIRE(counts.asymmetric());
    CHECK(counts.errorLow.value() > 0.0);
    CHECK(counts.errorHigh.value() > 0.0);
    CHECK(counts.errorHigh.value() == doctest::Approx(counts.error).epsilon(0.5));

    // The total is a derived sum, not a fit parameter, so it stays symmetric.
    CHECK(!minosResult.totalCounts.asymmetric());
}

TEST_CASE("a crystal ball peak fits end to end with calibrated counts")
{
    // A toy from a crystal ball truth, built with the same core math the
    // engine uses, on a flat background.
    FitComponent truth;
    truth.shape = ShapeKind::CrystalBall;
    truth.amplitude = { "amplitude", 70.0, false, std::nullopt, std::nullopt };
    truth.parameters = {
        { "mean", 50.0, false, std::nullopt, std::nullopt },
        { "sigma", 3.0, false, std::nullopt, std::nullopt },
        { "alpha", 1.2, false, std::nullopt, std::nullopt },
        { "n", 3.0, false, std::nullopt, std::nullopt },
    };
    FitRange fitRange{ kFitLo, kFitHi };
    double truthCounts = ComponentCounts(truth, fitRange);

    HistogramData data;
    data.name = "cb_toy";
    const int binCount = 200;
    for (int i = 0; i <= binCount; ++i)
    {
        data.binEdges.push_back(100.0 * i / binCount);
    }
    std::mt19937 rng(41);
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

    // alpha and n are fixed, as in real analyses where the tail shape comes
    // from a calibration: free on modest statistics they trade off against
    // sigma. Mean, sigma, and amplitude are fit.
    FitComponent peak = truth;
    peak.label = "Peak 1";
    peak.amplitude = { "amplitude", 55.0, false, 0.0, std::nullopt };
    peak.parameters[0].value = 49.0; // mean
    peak.parameters[1].value = 3.5;  // sigma
    peak.parameters[2] = { "alpha", 1.2, true, 0.0, std::nullopt };
    peak.parameters[3] = { "n", 3.0, true, 1.0, std::nullopt };
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

TEST_CASE("a landau peak fits end to end with calibrated counts")
{
    FitComponent truth;
    truth.shape = ShapeKind::Landau;
    truth.amplitude = { "amplitude", 70.0, false, std::nullopt, std::nullopt };
    truth.parameters = {
        { "mean", 50.0, false, std::nullopt, std::nullopt },
        { "scale", 2.5, false, std::nullopt, std::nullopt },
    };
    FitRange fitRange{ kFitLo, kFitHi };
    double truthCounts = ComponentCounts(truth, fitRange);

    HistogramData data;
    data.name = "landau_toy";
    const int binCount = 200;
    for (int i = 0; i <= binCount; ++i)
    {
        data.binEdges.push_back(100.0 * i / binCount);
    }
    std::mt19937 rng(43);
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

    FitComponent peak = truth;
    peak.label = "Peak 1";
    peak.amplitude = { "amplitude", 55.0, false, 0.0, std::nullopt };
    peak.parameters[0].value = 49.0;                                   // mean
    peak.parameters[1] = { "scale", 3.0, false, 0.0, std::nullopt };   // scale
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

TEST_CASE("GIGGLE's shapes match ROOT's own native functions")
{
    // The per-fit TF1NormSum cross-check uses GIGGLE's shape math on both
    // sides, so it cannot catch a discrepancy between our functions and
    // ROOT's native ones. These checks compare directly: identical functions
    // mean identical fits (same Minuit2, same data).
    FitRange range{ 0.0, 100.0 };

    SUBCASE("gaussian == TMath::Gaus")
    {
        FitComponent g;
        g.shape = ShapeKind::Gaussian;
        g.amplitude = { "amplitude", 1.0, false, std::nullopt, std::nullopt };
        g.parameters = { { "mean", 50.0, false, std::nullopt, std::nullopt },
                         { "sigma", 4.0, false, std::nullopt, std::nullopt } };
        for (double x = 25.0; x <= 75.0; x += 1.0)
        {
            CHECK(ShapeValue(g, range, x)
                  == doctest::Approx(TMath::Gaus(x, 50.0, 4.0, kFALSE)).epsilon(1e-9));
        }
    }

    SUBCASE("crystal ball == ROOT::Math::crystalball_function")
    {
        double mean = 50.0, sigma = 4.0, alpha = 1.3, n = 2.5;
        FitComponent cb;
        cb.shape = ShapeKind::CrystalBall;
        cb.amplitude = { "amplitude", 1.0, false, std::nullopt, std::nullopt };
        cb.parameters = { { "mean", mean, false, std::nullopt, std::nullopt },
                          { "sigma", sigma, false, std::nullopt, std::nullopt },
                          { "alpha", alpha, false, std::nullopt, std::nullopt },
                          { "n", n, false, std::nullopt, std::nullopt } };
        for (double x = 20.0; x <= 80.0; x += 1.0)
        {
            CHECK(ShapeValue(cb, range, x)
                  == doctest::Approx(ROOT::Math::crystalball_function(x, alpha, n, sigma, mean))
                         .epsilon(1e-9));
        }
    }

    SUBCASE("landau == TMath::Landau (our ported CERNLIB recipe)")
    {
        double mean = 50.0, scale = 3.0;
        FitComponent l;
        l.shape = ShapeKind::Landau;
        l.amplitude = { "amplitude", 1.0, false, std::nullopt, std::nullopt };
        l.parameters = { { "mean", mean, false, std::nullopt, std::nullopt },
                         { "scale", scale, false, std::nullopt, std::nullopt } };
        // Our shape places the standardized density's peak at the mean and
        // normalizes to 1 there; reproduce that from ROOT's TMath::Landau.
        const double off = 0.22278298;
        double rootPeak = TMath::Landau(-off, 0.0, 1.0, kFALSE);
        for (double x = 20.0; x <= 110.0; x += 1.0)
        {
            double rootProfile =
                TMath::Landau((x - mean) / scale - off, 0.0, 1.0, kFALSE) / rootPeak;
            CHECK(ShapeValue(l, range, x) == doctest::Approx(rootProfile).epsilon(1e-6));
        }
    }

    SUBCASE("lorentzian == TMath::BreitWigner")
    {
        // Our gamma is the HWHM; ROOT's BreitWigner gamma is the FWHM (2x).
        double mean = 50.0, gamma = 4.0;
        FitComponent lz;
        lz.shape = ShapeKind::Lorentzian;
        lz.amplitude = { "amplitude", 1.0, false, std::nullopt, std::nullopt };
        lz.parameters = { { "mean", mean, false, std::nullopt, std::nullopt },
                          { "gamma", gamma, false, std::nullopt, std::nullopt } };
        double bwPeak = TMath::BreitWigner(mean, mean, 2.0 * gamma);
        for (double x = 25.0; x <= 75.0; x += 1.0)
        {
            CHECK(ShapeValue(lz, range, x)
                  == doctest::Approx(TMath::BreitWigner(x, mean, 2.0 * gamma) / bwPeak).epsilon(1e-9));
        }
    }

    SUBCASE("voigt == TMath::Voigt")
    {
        // Our gamma is the Lorentzian HWHM; ROOT's lg is the FWHM (2x). Both
        // are approximations of the true Voigt, so they agree to ~1e-3.
        double mean = 50.0, sigma = 3.0, gamma = 2.0;
        FitComponent v;
        v.shape = ShapeKind::Voigt;
        v.amplitude = { "amplitude", 1.0, false, std::nullopt, std::nullopt };
        v.parameters = { { "mean", mean, false, std::nullopt, std::nullopt },
                         { "sigma", sigma, false, std::nullopt, std::nullopt },
                         { "gamma", gamma, false, std::nullopt, std::nullopt } };
        double vPeak = TMath::Voigt(0.0, sigma, 2.0 * gamma);
        for (double x = 38.0; x <= 62.0; x += 1.0)
        {
            CHECK(ShapeValue(v, range, x)
                  == doctest::Approx(TMath::Voigt(x - mean, sigma, 2.0 * gamma) / vPeak).epsilon(3e-3));
        }
    }

    SUBCASE("step == TMath::Erfc")
    {
        double edge = 50.0, width = 3.0;
        FitComponent st;
        st.shape = ShapeKind::Step;
        st.amplitude = { "amplitude", 1.0, false, std::nullopt, std::nullopt };
        st.parameters = { { "edge", edge, false, std::nullopt, std::nullopt },
                          { "width", width, false, std::nullopt, std::nullopt } };
        for (double x = 25.0; x <= 75.0; x += 1.0)
        {
            CHECK(ShapeValue(st, range, x)
                  == doctest::Approx(0.5 * TMath::Erfc((x - edge) / (std::sqrt(2.0) * width)))
                         .epsilon(1e-9));
        }
    }

    SUBCASE("polynomial and exponential backgrounds == ROOT pol/expo")
    {
        double pivot = (range.min + range.max) / 2.0; // RangePivot
        auto sample = [&](const FitComponent& component, TF1& root) {
            for (double x = 10.0; x <= 90.0; x += 5.0)
            {
                CHECK(ShapeValue(component, range, x) == doctest::Approx(root.Eval(x)).epsilon(1e-9));
            }
        };

        double slope = -0.01, curvature = 0.0004;

        FitComponent linear;
        linear.shape = ShapeKind::Linear;
        linear.amplitude = { "amplitude", 1.0, false, std::nullopt, std::nullopt };
        linear.parameters = { { "slope", slope, false, std::nullopt, std::nullopt } };
        TF1 pol1("fid_pol1", "pol1", range.min, range.max);
        pol1.SetParameters(1.0 - slope * pivot, slope);
        sample(linear, pol1);

        FitComponent quadratic;
        quadratic.shape = ShapeKind::Quadratic;
        quadratic.amplitude = { "amplitude", 1.0, false, std::nullopt, std::nullopt };
        quadratic.parameters = { { "slope", slope, false, std::nullopt, std::nullopt },
                                 { "curvature", curvature, false, std::nullopt, std::nullopt } };
        TF1 pol2("fid_pol2", "pol2", range.min, range.max);
        pol2.SetParameters(1.0 - slope * pivot + curvature * pivot * pivot,
                           slope - 2.0 * curvature * pivot, curvature);
        sample(quadratic, pol2);

        FitComponent exponential;
        exponential.shape = ShapeKind::Exponential;
        exponential.amplitude = { "amplitude", 1.0, false, std::nullopt, std::nullopt };
        exponential.parameters = { { "slope", -0.02, false, std::nullopt, std::nullopt } };
        TF1 expo("fid_expo", "expo", range.min, range.max);
        expo.SetParameters(0.02 * pivot, -0.02);
        sample(exponential, expo);

        FitComponent constant;
        constant.shape = ShapeKind::Constant;
        constant.amplitude = { "amplitude", 1.0, false, std::nullopt, std::nullopt };
        for (double x = 10.0; x <= 90.0; x += 20.0)
        {
            CHECK(ShapeValue(constant, range, x) == doctest::Approx(1.0));
        }
    }

    // gaussian_tail (gf3/Hypermet) and custom have no ROOT native equivalent.
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

TEST_CASE("an empty or invalid custom formula evaluates to zero on every call")
{
    // Regression: an invalid formula was cached and then returned for
    // evaluation, so TFormula::Eval spammed "not ready to execute" every
    // frame. Repeated calls (the cache-hit path) must still yield 0.
    double p[1] = { 1.0 };
    CHECK(EvaluateFormulaRaw("", p, 0, 1.0) == 0.0);
    CHECK(EvaluateFormulaRaw("", p, 0, 2.0) == 0.0);
    CHECK(EvaluateFormulaRaw("garbage_func(x)", p, 1, 1.0) == 0.0);
    CHECK(EvaluateFormulaRaw("garbage_func(x)", p, 1, 1.0) == 0.0); // cached invalid
    CHECK(EvaluateFormulaRaw("exp((x)", p, 0, 1.0) == 0.0);         // unbalanced
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
