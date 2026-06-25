#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cmath>

#include "core/FitModel.h"
#include "core/FitResult.h"
#include "core/HistogramData.h"
#include "core/Serialization.h"
#include "core/Shapes.h"
#include "core/Version.h"

using namespace giggle;

// A model exercising every serialization feature: several shapes, a custom
// formula, fixed parameters, and one- and two-sided bounds.
static FitModel MakeRichModel()
{
    FitModel model;
    model.range = { 100.0, 250.0 };
    model.statistic = FitStatistic::PoissonLikelihood;

    FitComponent peak1;
    peak1.label = "Peak 1";
    peak1.shape = ShapeKind::Gaussian;
    peak1.amplitude = { "amplitude", 66.0, false, 0.0, std::nullopt };
    peak1.parameters = {
        { "mean", 150.0, false, 140.0, 160.0 },
        { "sigma", 3.0, true, std::nullopt, std::nullopt },
    };
    model.peaks.push_back(peak1);

    FitComponent peak2;
    peak2.label = "Peak 2";
    peak2.shape = ShapeKind::Voigt;
    peak2.amplitude = { "amplitude", 16.0, false, 0.0, std::nullopt };
    peak2.parameters = {
        { "mean", 200.0, false, std::nullopt, std::nullopt },
        { "sigma", 3.0, false, std::nullopt, std::nullopt },
        { "gamma", 1.5, false, 0.0, std::nullopt },
    };
    model.peaks.push_back(peak2);

    FitComponent background;
    background.label = "Background";
    background.shape = ShapeKind::Linear;
    background.amplitude = { "amplitude", 13.0, false, 0.0, std::nullopt };
    background.parameters = {
        { "slope", -0.002, false, std::nullopt, std::nullopt },
    };
    model.background.push_back(background);

    FitComponent custom;
    custom.label = "Shelf";
    custom.shape = ShapeKind::Custom;
    custom.formula = "1.0/(1.0+exp((x-[0])/[1]))";
    custom.amplitude = { "amplitude", 0.3, false, 0.0, std::nullopt };
    custom.parameters = {
        { "edge", 180.0, false, std::nullopt, std::nullopt },
        { "width", 2.0, false, std::nullopt, std::nullopt },
    };
    model.background.push_back(custom);

    // Non-default fit settings, so the round-trip exercises every field.
    model.algorithm = MinimizerAlgorithm::Simplex;
    model.uncertainties = FitUncertainties::Minos;
    model.integrateBins = true;
    model.ignoreBinErrors = true;
    model.countEmptyBins = true;
    model.tolerance = 0.005;
    model.maxIterations = 500;

    return model;
}

static FitComponent MakeComponent(ShapeKind shape, double amplitude, std::vector<double> values)
{
    FitComponent component;
    component.shape = shape;
    component.amplitude = { "amplitude", amplitude, false, std::nullopt, std::nullopt };
    std::vector<std::string> names = ShapeParameterNames(shape);
    for (size_t i = 0; i < values.size(); ++i)
    {
        component.parameters.push_back({ i < names.size() ? names[i] : "p", values[i],
                                         false, std::nullopt, std::nullopt });
    }
    return component;
}

TEST_CASE("HistogramData reports its dimensions")
{
    HistogramData histogram;
    histogram.name = "h_test";
    histogram.binEdges = { 0.0, 1.0, 2.0, 3.0 };
    histogram.counts = { 5.0, 9.0, 4.0 };

    CHECK(histogram.IsValid());
    CHECK(histogram.BinCount() == 3);
    CHECK(histogram.XMin() == 0.0);
    CHECK(histogram.XMax() == 3.0);
    CHECK(histogram.BinError(1) == doctest::Approx(3.0)); // sqrt(9)

    histogram.errors = { 1.0, 2.0, 3.0 };
    CHECK(histogram.IsValid());
    CHECK(histogram.BinError(1) == 2.0);
}

TEST_CASE("HistogramData rejects inconsistent contents")
{
    HistogramData histogram;
    histogram.binEdges = { 0.0, 1.0, 2.0 };
    histogram.counts = { 1.0, 2.0, 3.0 }; // one count too many
    CHECK(!histogram.IsValid());

    histogram.counts = { 1.0, 2.0 };
    CHECK(histogram.IsValid());

    histogram.binEdges = { 0.0, 2.0, 1.0 }; // not ascending
    CHECK(!histogram.IsValid());
}

TEST_CASE("shape names round-trip and carry their parameter lists")
{
    const ShapeKind allKinds[] = {
        ShapeKind::Gaussian, ShapeKind::GaussianTail, ShapeKind::Lorentzian,
        ShapeKind::Voigt,    ShapeKind::Constant,     ShapeKind::Linear,
        ShapeKind::Quadratic, ShapeKind::Exponential, ShapeKind::Step,
        ShapeKind::Custom,
    };
    for (ShapeKind kind : allKinds)
    {
        std::string name = ShapeKindName(kind);
        CHECK(name != "unknown");
        REQUIRE(ShapeKindFromName(name).has_value());
        CHECK(ShapeKindFromName(name).value() == kind);
    }

    CHECK(ShapeParameterNames(ShapeKind::Gaussian) == std::vector<std::string>{ "mean", "sigma" });
    CHECK(ShapeParameterNames(ShapeKind::Constant).empty());
    CHECK(!ShapeKindFromName("not_a_shape").has_value());
}

TEST_CASE("every shape is 1 at its reference point, so amplitude = density there")
{
    FitRange range{ 100.0, 250.0 };
    double pivot = RangePivot(range);

    CHECK(ShapeValue(MakeComponent(ShapeKind::Gaussian, 1, { 150.0, 5.0 }), range, 150.0) == doctest::Approx(1.0));
    CHECK(ShapeValue(MakeComponent(ShapeKind::GaussianTail, 1, { 150.0, 5.0, 0.3, 8.0 }), range, 150.0) == doctest::Approx(1.0));
    CHECK(ShapeValue(MakeComponent(ShapeKind::Lorentzian, 1, { 170.0, 4.0 }), range, 170.0) == doctest::Approx(1.0));
    // The step is 1 on its low-energy plateau.
    CHECK(ShapeValue(MakeComponent(ShapeKind::Step, 1, { 175.0, 2.0 }), range, 120.0) == doctest::Approx(1.0));
    CHECK(ShapeValue(MakeComponent(ShapeKind::Step, 1, { 175.0, 2.0 }), range, 175.0) == doctest::Approx(0.5));
    CHECK(ShapeValue(MakeComponent(ShapeKind::Step, 1, { 175.0, 2.0 }), range, 240.0) == doctest::Approx(0.0));
    CHECK(ShapeValue(MakeComponent(ShapeKind::Constant, 1, {}), range, pivot) == doctest::Approx(1.0));
    CHECK(ShapeValue(MakeComponent(ShapeKind::Linear, 1, { -0.002 }), range, pivot) == doctest::Approx(1.0));
    CHECK(ShapeValue(MakeComponent(ShapeKind::Quadratic, 1, { -0.002, 1e-5 }), range, pivot) == doctest::Approx(1.0));
    CHECK(ShapeValue(MakeComponent(ShapeKind::Exponential, 1, { -0.01 }), range, pivot) == doctest::Approx(1.0));

    // Amplitude is the density at the reference point.
    FitComponent peak = MakeComponent(ShapeKind::Gaussian, 66.0, { 150.0, 5.0 });
    CHECK(ComponentDensity(peak, range, 150.0) == doctest::Approx(66.0));
}

TEST_CASE("component counts match the numeric integral of the density")
{
    FitRange range{ 100.0, 250.0 };

    auto numericIntegral = [&](const FitComponent& component) {
        const int steps = 20000;
        double sum = 0.0;
        double dx = (range.max - range.min) / steps;
        for (int i = 0; i < steps; ++i)
        {
            double left = ComponentDensity(component, range, range.min + i * dx);
            double right = ComponentDensity(component, range, range.min + (i + 1) * dx);
            sum += 0.5 * (left + right) * dx;
        }
        return sum;
    };

    // Includes a Gaussian hanging off the range edge: counts stay the
    // in-range portion, well-defined regardless.
    const FitComponent cases[] = {
        MakeComponent(ShapeKind::Gaussian, 66.0, { 150.0, 5.0 }),
        MakeComponent(ShapeKind::Gaussian, 66.0, { 245.0, 8.0 }),
        MakeComponent(ShapeKind::GaussianTail, 66.0, { 150.0, 5.0, 0.3, 8.0 }),
        MakeComponent(ShapeKind::GaussianTail, 66.0, { 150.0, 5.0, 0.9, 20.0 }), // heavy tail
        MakeComponent(ShapeKind::Lorentzian, 40.0, { 175.0, 4.0 }),
        MakeComponent(ShapeKind::Constant, 3.3, {}),
        MakeComponent(ShapeKind::Linear, 13.0, { -0.002 }),
        MakeComponent(ShapeKind::Quadratic, 13.0, { -0.002, 1e-5 }),
        MakeComponent(ShapeKind::Exponential, 13.0, { -0.01 }),
        MakeComponent(ShapeKind::Step, 13.0, { 175.0, 2.0 }),
    };
    for (const FitComponent& component : cases)
    {
        CHECK(ComponentCounts(component, range)
              == doctest::Approx(numericIntegral(component)).epsilon(1e-6));
    }
}

TEST_CASE("count errors propagate through the covariance")
{
    // A Gaussian fully inside a wide range: N = A * sigma * sqrt(2 pi),
    // so the analytic derivatives are known exactly.
    FitRange range{ 0.0, 300.0 };
    FitComponent peak = MakeComponent(ShapeKind::Gaussian, 50.0, { 150.0, 4.0 });

    double rootTwoPi = std::sqrt(2.0 * M_PI);
    CHECK(ComponentCounts(peak, range) == doctest::Approx(50.0 * 4.0 * rootTwoPi).epsilon(1e-9));

    std::vector<double> gradient = ComponentCountsGradient(peak, range);
    REQUIRE(gradient.size() == 3);
    CHECK(gradient[0] == doctest::Approx(4.0 * rootTwoPi).epsilon(1e-9));   // dN/dA
    CHECK(gradient[1] == doctest::Approx(0.0).epsilon(1e-3));               // dN/dmean
    CHECK(gradient[2] == doctest::Approx(50.0 * rootTwoPi).epsilon(1e-4));  // dN/dsigma

    // Amplitude uncertainty only: sigma_N = I * sigma_A exactly.
    std::vector<std::vector<double>> covariance = {
        { 4.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0 },
    };
    ValueWithError counts = ComponentCountsWithError(peak, range, covariance);
    CHECK(counts.error == doctest::Approx(2.0 * 4.0 * rootTwoPi).epsilon(1e-6));

    // Adding sigma variance and an amplitude-sigma correlation follows the
    // standard quadratic form.
    covariance[2][2] = 0.01;
    covariance[0][2] = covariance[2][0] = -0.1;
    double expected = gradient[0] * gradient[0] * 4.0
                      + gradient[2] * gradient[2] * 0.01
                      + 2.0 * gradient[0] * gradient[2] * -0.1;
    counts = ComponentCountsWithError(peak, range, covariance);
    CHECK(counts.error == doctest::Approx(std::sqrt(expected)).epsilon(1e-6));
}

TEST_CASE("model curves sample the range and sum to the total")
{
    FitModel model = MakeRichModel(); // contains Voigt and custom, which draw as 0
    FitCurves curves = SampleModelCurves(model, 101);

    REQUIRE(curves.x.size() == 101);
    CHECK(curves.x.front() == doctest::Approx(model.range.min));
    CHECK(curves.x.back() == doctest::Approx(model.range.max));
    REQUIRE(curves.components.size() == 4); // 2 peaks + 2 background entries

    for (size_t i = 0; i < curves.x.size(); ++i)
    {
        double sum = 0.0;
        for (const std::vector<double>& component : curves.components)
        {
            sum += component[i];
        }
        CHECK(curves.total[i] == doctest::Approx(sum));
    }

    // Voigt and custom shapes have no curve yet.
    CHECK(ComponentDensity(model.background[1], model.range, 180.0) == 0.0);
}

TEST_CASE("curves given a histogram are scaled to counts per bin")
{
    // A model curve must overlay histogram data, which is in counts per
    // bin, not in density units: the scale factor is the local bin width.
    FitModel model;
    model.range = { 0.0, 10.0 };
    model.peaks.push_back(MakeComponent(ShapeKind::Constant, 10.0, {}));

    HistogramData histogram;
    histogram.binEdges = { 0.0, 0.5, 1.0, 1.5, 2.0 }; // bin width 0.5
    histogram.counts = { 1.0, 1.0, 1.0, 1.0 };

    // Density: the amplitude itself.
    FitCurves density = SampleModelCurves(model, 11);
    CHECK(density.total[5] == doctest::Approx(10.0));

    // In data units: 10 per x-unit * 0.5 wide bins = 5 counts per bin.
    FitCurves scaled = SampleModelCurves(model, 11, &histogram);
    CHECK(scaled.total[5] == doctest::Approx(5.0));
}

TEST_CASE("fit ranges snap to bin edges")
{
    HistogramData histogram;
    histogram.binEdges = { 0.0, 1.0, 2.0, 3.0, 4.0, 5.0 };
    histogram.counts = { 1.0, 1.0, 1.0, 1.0, 1.0 };

    // Nearest edges win.
    FitRange snapped = SnapRangeToBinEdges(histogram, { 0.9, 4.4 });
    CHECK(snapped.min == 1.0);
    CHECK(snapped.max == 4.0);

    // Already on edges: unchanged.
    snapped = SnapRangeToBinEdges(histogram, { 1.0, 4.0 });
    CHECK(snapped.min == 1.0);
    CHECK(snapped.max == 4.0);

    // Outside the histogram: clamped to it.
    snapped = SnapRangeToBinEdges(histogram, { -10.0, 10.0 });
    CHECK(snapped.min == 0.0);
    CHECK(snapped.max == 5.0);

    // Both ends in the same bin: kept at least one bin wide.
    snapped = SnapRangeToBinEdges(histogram, { 2.4, 2.6 });
    CHECK(snapped.max - snapped.min == doctest::Approx(1.0));
}

TEST_CASE("counts in range follow the fit's bin selection")
{
    HistogramData histogram;
    histogram.binEdges = { 0.0, 1.0, 2.0, 3.0, 4.0 };
    histogram.counts = { 9.0, 16.0, 25.0, 36.0 };

    // Bins with centers 1.5 and 2.5: 16 + 25, error sqrt(16 + 25).
    ValueWithError counts = CountsInRange(histogram, { 1.0, 3.0 });
    CHECK(counts.value == 41.0);
    CHECK(counts.error == doctest::Approx(std::sqrt(41.0)));

    // Explicit bin errors are used when present.
    histogram.errors = { 1.0, 2.0, 3.0, 4.0 };
    counts = CountsInRange(histogram, { 1.0, 3.0 });
    CHECK(counts.value == 41.0);
    CHECK(counts.error == doctest::Approx(std::sqrt(4.0 + 9.0)));

    // Region statistics: bin moments over the same selection. Bins at
    // centers 1.5 (16 counts) and 2.5 (25 counts).
    RegionStats stats = AnalyzeRange(histogram, { 1.0, 3.0 });
    CHECK(stats.counts.value == 41.0);
    double centroid = (16.0 * 1.5 + 25.0 * 2.5) / 41.0;
    CHECK(stats.centroid == doctest::Approx(centroid));
    double second = (16.0 * 1.5 * 1.5 + 25.0 * 2.5 * 2.5) / 41.0 - centroid * centroid;
    CHECK(stats.rms == doctest::Approx(std::sqrt(second)));
}

TEST_CASE("FitModel survives a JSON round-trip unchanged")
{
    FitModel original = MakeRichModel();

    Json serialized = ToJson(original);
    FitModel restored = FitModelFromJson(serialized);
    Json reserialized = ToJson(restored);

    CHECK(serialized == reserialized);

    // Spot checks on the restored model itself.
    CHECK(restored.range.min == 100.0);
    CHECK(restored.range.max == 250.0);
    CHECK(restored.statistic == FitStatistic::PoissonLikelihood);
    REQUIRE(restored.peaks.size() == 2);
    REQUIRE(restored.background.size() == 2);

    CHECK(restored.peaks[0].amplitude.value == 66.0);
    CHECK(restored.peaks[0].amplitude.lowerBound == 0.0);
    CHECK(restored.peaks[0].parameters[1].fixed);
    CHECK(restored.peaks[0].parameters[0].lowerBound == 140.0);
    CHECK(restored.peaks[0].parameters[0].upperBound == 160.0);
    CHECK(!restored.peaks[1].parameters[0].lowerBound.has_value());
    CHECK(restored.background[1].shape == ShapeKind::Custom);
    CHECK(restored.background[1].formula == "1.0/(1.0+exp((x-[0])/[1]))");

    CHECK(restored.algorithm == MinimizerAlgorithm::Simplex);
    CHECK(restored.uncertainties == FitUncertainties::Minos);
    CHECK(restored.integrateBins);
    CHECK(restored.ignoreBinErrors);
    CHECK(restored.countEmptyBins);
    CHECK(restored.tolerance == doctest::Approx(0.005));
    CHECK(restored.maxIterations == 500);
}

TEST_CASE("a preset without a settings block loads with default fit settings")
{
    // Presets written before fit settings existed must still load, falling
    // back to the defaults rather than throwing.
    Json json = ToJson(MakeRichModel());
    json.erase("settings");

    FitModel model = FitModelFromJson(json);
    CHECK(model.algorithm == MinimizerAlgorithm::Migrad);
    CHECK(model.uncertainties == FitUncertainties::Parabolic);
    CHECK(!model.integrateBins);
    CHECK(!model.ignoreBinErrors);
    CHECK(!model.countEmptyBins);
    CHECK(model.tolerance == 0.0);
    CHECK(model.maxIterations == 0);
}

TEST_CASE("FitModelFromJson rejects malformed documents")
{
    Json valid = ToJson(MakeRichModel());

    Json missingRange = valid;
    missingRange.erase("fit_range");
    CHECK_THROWS(FitModelFromJson(missingRange));

    Json badShape = valid;
    badShape["peaks"][0]["shape"] = "not_a_shape";
    CHECK_THROWS(FitModelFromJson(badShape));

    Json badStatistic = valid;
    badStatistic["statistic"] = "least_absolute_deviations";
    CHECK_THROWS(FitModelFromJson(badStatistic));
}

TEST_CASE("results documents carry schema version, provenance, and named parameters")
{
    FitModel model = MakeRichModel();

    FitResult result;
    result.converged = true;
    result.message = "fit converged";
    result.chiSquare = 42.5;
    result.degreesOfFreedom = 37;
    result.peaks = {
        { "Peak 1", { 512.0, 25.0 }, { 66.2, 3.1 }, { { 150.2, 0.1 }, { 3.0, 0.0 } } },
        { "Peak 2", { 118.0, 14.0 }, { 15.8, 2.0 }, { { 199.8, 0.3 }, { 2.9, 0.2 }, { 1.4, 0.3 } } },
    };
    result.background = {
        { "Background", { 1980.0, 60.0 }, { 13.1, 0.4 }, { { -0.0021, 0.0004 } } },
        { "Shelf", { 48.0, 9.0 }, { 0.31, 0.06 }, { { 180.4, 0.8 }, { 2.1, 0.5 } } },
    };
    result.totalCounts = { 2658.0, 68.0 };
    result.covariance = { { 625.0, 1.0 }, { 1.0, 0.01 } };

    // An asymmetric (MINOS) error on one parameter, to exercise that path.
    result.peaks[0].amplitude.errorLow = 2.9;
    result.peaks[0].amplitude.errorHigh = 3.3;

    Provenance provenance = MakeProvenance("run52.root", "spectra/h_ex");
    Json document = MakeResultsDocument(provenance, model, result);

    CHECK(document.at("schema_version") == kSchemaVersion);
    CHECK(document.at("provenance").at("source_file") == "run52.root");
    CHECK(document.at("provenance").at("histogram") == "spectra/h_ex");
    CHECK(document.at("provenance").at("giggle_version") == Version());
    CHECK(document.at("provenance").at("timestamp").get<std::string>().size() == 20);

    CHECK(document.at("fit").at("peaks").size() == 2);
    CHECK(document.at("result").at("converged") == true);
    CHECK(document.at("result").at("peaks")[0].at("counts_in_range").at("value") == 512.0);
    CHECK(document.at("result").at("peaks")[0].at("amplitude").at("value") == 66.2);
    CHECK(document.at("result").at("peaks")[0].at("parameters")[0].at("name") == "mean");
    CHECK(document.at("result").at("background")[1].at("parameters")[1].at("name") == "width");
    CHECK(document.at("result").at("total_counts_in_range").at("value") == 2658.0);
    CHECK(document.at("result").at("covariance")[0][0] == 625.0);

    // Asymmetric errors appear only where set; symmetric ones omit them.
    CHECK(document.at("result").at("peaks")[0].at("amplitude").at("error_low") == 2.9);
    CHECK(document.at("result").at("peaks")[0].at("amplitude").at("error_high") == 3.3);
    CHECK(!document.at("result").at("peaks")[0].at("counts_in_range").contains("error_low"));
}

TEST_CASE("centroid and FWHM derive from the fitted parameters")
{
    ComponentResult gaussian;
    gaussian.parameters = { { 150.2, 0.1 }, { 3.0, 0.2 } }; // mean, sigma

    auto centroid = PeakCentroid(ShapeKind::Gaussian, gaussian);
    REQUIRE(centroid.has_value());
    CHECK(centroid->value == 150.2);
    CHECK(centroid->error == 0.1);

    auto fwhm = PeakFWHM(ShapeKind::Gaussian, gaussian);
    REQUIRE(fwhm.has_value());
    CHECK(fwhm->value == doctest::Approx(2.354820045 * 3.0));
    CHECK(fwhm->error == doctest::Approx(2.354820045 * 0.2));

    ComponentResult lorentzian;
    lorentzian.parameters = { { 170.0, 0.3 }, { 1.5, 0.1 } }; // mean, gamma (HWHM)
    auto lorentzianFwhm = PeakFWHM(ShapeKind::Lorentzian, lorentzian);
    REQUIRE(lorentzianFwhm.has_value());
    CHECK(lorentzianFwhm->value == doctest::Approx(3.0));

    // Backgrounds have neither.
    ComponentResult background;
    background.parameters = { { -0.002, 0.0004 } };
    CHECK(!PeakCentroid(ShapeKind::Linear, background).has_value());
    CHECK(!PeakFWHM(ShapeKind::Linear, background).has_value());
}

TEST_CASE("results export carries warnings, the cross-check, and a CSV table")
{
    FitModel model = MakeRichModel();

    FitResult result;
    result.converged = true;
    result.chiSquare = 42.5;
    result.degreesOfFreedom = 37;
    result.peaks = {
        { "Peak 1", { 512.0, 25.0 }, { 66.2, 3.1 }, { { 150.2, 0.1 }, { 3.0, 0.0 } } },
    };
    result.background = {
        { "Background", { 1980.0, 60.0 }, { 13.1, 0.4 }, { { -0.0021, 0.0004 } } },
    };
    result.totalCounts = { 2492.0, 65.0 };
    result.normSumCheck = { true, true, "largest count deviation 0.02%" };
    result.warnings = { "Peak 1 extends past the fit range; its counts cover only the in-range part" };

    Provenance provenance = MakeProvenance("run52.root", "spectra/h_ex");

    Json document = MakeResultsDocument(provenance, model, result);
    CHECK(document.at("result").at("reduced_chi_square").get<double>()
          == doctest::Approx(42.5 / 37.0));
    CHECK(document.at("result").at("normsum_cross_check").at("agreed") == true);
    CHECK(document.at("result").at("warnings").size() == 1);
    CHECK(document.at("result").at("peaks")[0].at("centroid").at("value") == 150.2);
    CHECK(document.at("result").at("peaks")[0].at("fwhm").at("value")
          == doctest::Approx(2.354820045 * 3.0));

    std::string csv = MakeResultsCsv(provenance, model, result);
    CHECK(csv.find("component,shape,counts,counts_error,centroid,") == 0);
    CHECK(csv.find("Peak 1,gaussian,512,25,150.2,0.1,") != std::string::npos);
    CHECK(csv.find("Background,linear,1980,60,,,,,13.1,0.4,") != std::string::npos);
    CHECK(csv.find("spectra/h_ex,run52.root,") != std::string::npos);

    // Two component rows plus the header.
    int lines = 0;
    for (char c : csv)
    {
        lines += c == '\n' ? 1 : 0;
    }
    CHECK(lines == 3);
}

TEST_CASE("a clicked peak is guessed from the data around the click")
{
    // A clean Gaussian on a flat shelf: 400 counts tall at x = 50 with
    // sigma 3, over a baseline of 50 counts per bin. Bin width 0.5.
    HistogramData histogram;
    const int binCount = 200;
    for (int i = 0; i <= binCount; ++i)
    {
        histogram.binEdges.push_back(100.0 * i / binCount);
    }
    for (int i = 0; i < binCount; ++i)
    {
        double center = 0.5 * (histogram.binEdges[i] + histogram.binEdges[i + 1]);
        double z = (center - 50.0) / 3.0;
        histogram.counts.push_back(50.0 + 400.0 * std::exp(-0.5 * z * z));
    }

    FitRange range{ 30.0, 70.0 };

    // A slightly off-center click still finds the peak.
    FitComponent guess = SuggestGaussianPeak(histogram, range, 49.2);
    CHECK(guess.shape == ShapeKind::Gaussian);
    CHECK(guess.parameters[0].value == doctest::Approx(50.0).epsilon(0.02));
    CHECK(guess.parameters[1].value == doctest::Approx(3.0).epsilon(0.35));
    // Height above baseline, in density units: ~400 counts / 0.5 width.
    CHECK(guess.amplitude.value == doctest::Approx(800.0).epsilon(0.15));
    CHECK(guess.amplitude.lowerBound == 0.0);

    // A click in flat background gives a small, sane guess instead of
    // something absurd.
    FitComponent flat = SuggestGaussianPeak(histogram, range, 35.0);
    CHECK(flat.parameters[1].value >= 0.5);                       // at least a bin
    CHECK(flat.parameters[1].value <= (range.max - range.min) / 4.0);
}

TEST_CASE("peak labels keep counting upward")
{
    std::vector<FitComponent> peaks;
    CHECK(NextPeakLabel(peaks) == "Peak 1");

    FitComponent peak;
    peak.label = "Peak 1";
    peaks.push_back(peak);
    peak.label = "Peak 7";
    peaks.push_back(peak);
    CHECK(NextPeakLabel(peaks) == "Peak 8");
}

TEST_CASE("the voigt profile behaves at its limits and in between")
{
    FitRange range{ 0.0, 300.0 };

    // Center value is 1 by construction.
    FitComponent voigt = MakeComponent(ShapeKind::Voigt, 10.0, { 150.0, 3.0, 1.5 });
    CHECK(ShapeValue(voigt, range, 150.0) == doctest::Approx(1.0));

    // gamma -> 0: agrees with the Gaussian.
    FitComponent nearlyGauss = MakeComponent(ShapeKind::Voigt, 1.0, { 150.0, 3.0, 1e-15 });
    FitComponent gauss = MakeComponent(ShapeKind::Gaussian, 1.0, { 150.0, 3.0 });
    for (double x : { 145.0, 150.0, 153.0, 160.0 })
    {
        CHECK(ShapeValue(nearlyGauss, range, x)
              == doctest::Approx(ShapeValue(gauss, range, x)).epsilon(1e-3));
    }

    // sigma -> 0: agrees with the Lorentzian.
    FitComponent nearlyLorentz = MakeComponent(ShapeKind::Voigt, 1.0, { 150.0, 1e-15, 2.0 });
    FitComponent lorentz = MakeComponent(ShapeKind::Lorentzian, 1.0, { 150.0, 2.0 });
    for (double x : { 145.0, 150.0, 153.0, 160.0 })
    {
        CHECK(ShapeValue(nearlyLorentz, range, x)
              == doctest::Approx(ShapeValue(lorentz, range, x)).epsilon(1e-3));
    }

    // The numeric integral makes counts work like any other shape:
    // N = A * integral, checked against a trapezoid sum of the density.
    double counts = ComponentCounts(voigt, range);
    double sum = 0.0;
    int steps = 4000;
    double dx = (range.max - range.min) / steps;
    for (int i = 0; i < steps; ++i)
    {
        sum += ComponentDensity(voigt, range, range.min + (i + 0.5) * dx) * dx;
    }
    CHECK(counts == doctest::Approx(sum).epsilon(1e-4));

    // The FWHM approximation lands where the shape really crosses half max.
    ComponentResult fitted;
    fitted.parameters = { { 150.0, 0.0 }, { 3.0, 0.1 }, { 1.5, 0.1 } };
    auto fwhm = PeakFWHM(ShapeKind::Voigt, fitted);
    REQUIRE(fwhm.has_value());
    double half = ShapeValue(voigt, range, 150.0 + fwhm->value / 2.0);
    CHECK(half == doctest::Approx(0.5).epsilon(0.01));
    CHECK(fwhm->error > 0.0);
}

TEST_CASE("the tailed gaussian reduces to the gaussian and reports an honest FWHM")
{
    FitRange range{ 100.0, 250.0 };

    // tail_fraction -> 0: identical to the pure gaussian everywhere.
    FitComponent noTail = MakeComponent(ShapeKind::GaussianTail, 1.0, { 150.0, 5.0, 0.0, 8.0 });
    FitComponent gauss = MakeComponent(ShapeKind::Gaussian, 1.0, { 150.0, 5.0 });
    for (double x : { 130.0, 145.0, 150.0, 160.0, 180.0 })
    {
        CHECK(ShapeValue(noTail, range, x)
              == doctest::Approx(ShapeValue(gauss, range, x)).epsilon(1e-9));
    }

    // FWHM without a tail matches the gaussian formula; with a tail the
    // numeric crossings sit exactly at half maximum, left side wider.
    ComponentResult fitted;
    fitted.parameters = { { 150.0, 0.0 }, { 5.0, 0.1 }, { 0.0, 0.0 }, { 8.0, 0.0 } };
    auto noTailFwhm = PeakFWHM(ShapeKind::GaussianTail, fitted);
    REQUIRE(noTailFwhm.has_value());
    CHECK(noTailFwhm->value == doctest::Approx(2.354820045 * 5.0).epsilon(1e-6));

    fitted.parameters = { { 150.0, 0.0 }, { 5.0, 0.1 }, { 0.4, 0.05 }, { 8.0, 0.5 } };
    auto tailedFwhm = PeakFWHM(ShapeKind::GaussianTail, fitted);
    REQUIRE(tailedFwhm.has_value());
    CHECK(tailedFwhm->value > 2.354820045 * 5.0); // the tail widens the peak
    CHECK(tailedFwhm->error > 0.0);

    // The FWHM is the distance between the actual half-max crossings: find
    // them by bisection on the shape and compare.
    FitComponent tailed = MakeComponent(ShapeKind::GaussianTail, 1.0, { 150.0, 5.0, 0.4, 8.0 });
    auto crossing = [&](double direction) {
        double inside = 0.0;
        double outside = 5.0;
        while (ShapeValue(tailed, range, 150.0 + direction * outside) > 0.5)
        {
            inside = outside;
            outside *= 2.0;
        }
        for (int i = 0; i < 60; ++i)
        {
            double middle = 0.5 * (inside + outside);
            (ShapeValue(tailed, range, 150.0 + direction * middle) > 0.5 ? inside : outside) = middle;
        }
        return 0.5 * (inside + outside);
    };
    double leftWidth = crossing(-1.0);
    double rightWidth = crossing(+1.0);
    CHECK(leftWidth > rightWidth); // the tail is on the low-energy side
    CHECK(tailedFwhm->value == doctest::Approx(leftWidth + rightWidth).epsilon(1e-6));
}

TEST_CASE("custom shapes flow through the evaluator hook")
{
    // A stand-in evaluator: a triangle shape, no ROOT needed in core tests.
    SetCustomShapeEvaluator([](const FitComponent& component, double x) {
        double center = component.parameters.empty() ? 0.0 : component.parameters[0].value;
        double width = component.parameters.size() > 1 ? component.parameters[1].value : 1.0;
        double distance = std::abs(x - center);
        return distance < width ? 1.0 - distance / width : 0.0;
    });
    CHECK(HasCustomShapeEvaluator());

    FitRange range{ 0.0, 100.0 };
    FitComponent triangle;
    triangle.shape = ShapeKind::Custom;
    triangle.formula = "(unused by the stand-in)";
    triangle.amplitude = { "amplitude", 6.0, false, std::nullopt, std::nullopt };
    triangle.parameters = {
        { "p0", 50.0, false, std::nullopt, std::nullopt },
        { "p1", 10.0, false, std::nullopt, std::nullopt },
    };

    CHECK(ComponentDensity(triangle, range, 50.0) == doctest::Approx(6.0));
    CHECK(ComponentDensity(triangle, range, 55.0) == doctest::Approx(3.0));
    // Triangle area: width * peak = 10 * 6 = 60.
    CHECK(ComponentCounts(triangle, range) == doctest::Approx(60.0).epsilon(1e-3));

    SetCustomShapeEvaluator(nullptr);
    CHECK(ComponentDensity(triangle, range, 50.0) == 0.0);
}

TEST_CASE("version is defined")
{
    CHECK(std::string(Version()) != "");
    CHECK(std::string(Version()) != "unknown");
}
