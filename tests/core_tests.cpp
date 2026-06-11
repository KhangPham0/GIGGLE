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
    peak1.yield = { "yield", 500.0, false, 0.0, std::nullopt };
    peak1.parameters = {
        { "mean", 150.0, false, 140.0, 160.0 },
        { "sigma", 3.0, true, std::nullopt, std::nullopt },
    };
    model.peaks.push_back(peak1);

    FitComponent peak2;
    peak2.label = "Peak 2";
    peak2.shape = ShapeKind::Voigt;
    peak2.yield = { "yield", 120.0, false, 0.0, std::nullopt };
    peak2.parameters = {
        { "mean", 200.0, false, std::nullopt, std::nullopt },
        { "sigma", 3.0, false, std::nullopt, std::nullopt },
        { "gamma", 1.5, false, 0.0, std::nullopt },
    };
    model.peaks.push_back(peak2);

    FitComponent background;
    background.label = "Background";
    background.shape = ShapeKind::Linear;
    background.yield = { "yield", 2000.0, false, 0.0, std::nullopt };
    background.parameters = {
        { "slope", -0.002, false, std::nullopt, std::nullopt },
    };
    model.background.push_back(background);

    FitComponent custom;
    custom.label = "Shelf";
    custom.shape = ShapeKind::Custom;
    custom.formula = "1.0/(1.0+exp((x-[0])/[1]))";
    custom.yield = { "yield", 50.0, false, 0.0, std::nullopt };
    custom.parameters = {
        { "edge", 180.0, false, std::nullopt, std::nullopt },
        { "width", 2.0, false, std::nullopt, std::nullopt },
    };
    model.background.push_back(custom);

    return model;
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
        ShapeKind::Gaussian, ShapeKind::Lorentzian, ShapeKind::Voigt,
        ShapeKind::Constant, ShapeKind::Linear,     ShapeKind::Quadratic,
        ShapeKind::Exponential, ShapeKind::Custom,
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

    CHECK(restored.peaks[0].parameters[1].fixed);
    CHECK(restored.peaks[0].parameters[0].lowerBound == 140.0);
    CHECK(restored.peaks[0].parameters[0].upperBound == 160.0);
    CHECK(!restored.peaks[1].parameters[0].lowerBound.has_value());
    CHECK(restored.background[1].shape == ShapeKind::Custom);
    CHECK(restored.background[1].formula == "1.0/(1.0+exp((x-[0])/[1]))");
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
        { "Peak 1", { 512.0, 25.0 }, { { 150.2, 0.1 }, { 3.0, 0.0 } } },
        { "Peak 2", { 118.0, 14.0 }, { { 199.8, 0.3 }, { 2.9, 0.2 }, { 1.4, 0.3 } } },
    };
    result.background = {
        { "Background", { 1980.0, 60.0 }, { { -0.0021, 0.0004 } } },
        { "Shelf", { 48.0, 9.0 }, { { 180.4, 0.8 }, { 2.1, 0.5 } } },
    };
    result.covariance = { { 625.0, 1.0 }, { 1.0, 0.01 } };

    Provenance provenance = MakeProvenance("run52.root", "spectra/h_ex");
    Json document = MakeResultsDocument(provenance, model, result);

    CHECK(document.at("schema_version") == kSchemaVersion);
    CHECK(document.at("provenance").at("source_file") == "run52.root");
    CHECK(document.at("provenance").at("histogram") == "spectra/h_ex");
    CHECK(document.at("provenance").at("giggle_version") == Version());
    CHECK(document.at("provenance").at("timestamp").get<std::string>().size() == 20);

    CHECK(document.at("fit").at("peaks").size() == 2);
    CHECK(document.at("result").at("converged") == true);
    CHECK(document.at("result").at("peaks")[0].at("yield").at("value") == 512.0);
    CHECK(document.at("result").at("peaks")[0].at("parameters")[0].at("name") == "mean");
    CHECK(document.at("result").at("background")[1].at("parameters")[1].at("name") == "width");
    CHECK(document.at("result").at("covariance")[0][0] == 625.0);
}

// Numerically integrates a component over the range with the trapezoid rule.
static double IntegrateComponent(const FitComponent& component, const FitRange& range, int steps = 20000)
{
    double sum = 0.0;
    double dx = (range.max - range.min) / steps;
    for (int i = 0; i < steps; ++i)
    {
        double left = ComponentValue(component, range, range.min + i * dx);
        double right = ComponentValue(component, range, range.min + (i + 1) * dx);
        sum += 0.5 * (left + right) * dx;
    }
    return sum;
}

TEST_CASE("every implemented shape integrates to its yield over the fit range")
{
    // The in-range count convention: yield * shape / norm must integrate to
    // the yield, whatever the shape and wherever the peak sits.
    FitRange range{ 100.0, 250.0 };

    auto makeComponent = [](ShapeKind shape, std::vector<double> values) {
        FitComponent component;
        component.shape = shape;
        component.yield = { "yield", 500.0, false, std::nullopt, std::nullopt };
        std::vector<std::string> names = ShapeParameterNames(shape);
        for (size_t i = 0; i < values.size(); ++i)
        {
            component.parameters.push_back({ i < names.size() ? names[i] : "p", values[i],
                                             false, std::nullopt, std::nullopt });
        }
        return component;
    };

    // Includes a Gaussian hanging off the range edge: the yield still means
    // counts inside the range.
    CHECK(IntegrateComponent(makeComponent(ShapeKind::Gaussian, { 150.0, 5.0 }), range) == doctest::Approx(500.0).epsilon(1e-6));
    CHECK(IntegrateComponent(makeComponent(ShapeKind::Gaussian, { 245.0, 8.0 }), range) == doctest::Approx(500.0).epsilon(1e-6));
    CHECK(IntegrateComponent(makeComponent(ShapeKind::Lorentzian, { 175.0, 4.0 }), range) == doctest::Approx(500.0).epsilon(1e-6));
    CHECK(IntegrateComponent(makeComponent(ShapeKind::Constant, {}), range) == doctest::Approx(500.0).epsilon(1e-6));
    CHECK(IntegrateComponent(makeComponent(ShapeKind::Linear, { -0.002 }), range) == doctest::Approx(500.0).epsilon(1e-6));
    CHECK(IntegrateComponent(makeComponent(ShapeKind::Quadratic, { -0.002, 0.00001 }), range) == doctest::Approx(500.0).epsilon(1e-6));
    CHECK(IntegrateComponent(makeComponent(ShapeKind::Exponential, { -0.01 }), range) == doctest::Approx(500.0).epsilon(1e-6));
}

TEST_CASE("gaussian shape integral matches the closed form over a wide range")
{
    FitComponent gaussian;
    gaussian.shape = ShapeKind::Gaussian;
    gaussian.parameters = {
        { "mean", 0.0, false, std::nullopt, std::nullopt },
        { "sigma", 2.0, false, std::nullopt, std::nullopt },
    };
    // Far enough out that the missing tails are negligible: sigma*sqrt(2*pi).
    CHECK(ShapeIntegral(gaussian, -100.0, 100.0) == doctest::Approx(2.0 * std::sqrt(2.0 * M_PI)).epsilon(1e-9));
}

TEST_CASE("model curves sample the range and sum to the total")
{
    FitModel model = MakeRichModel(); // contains a custom shape, which draws as 0
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
    FitComponent custom = model.background[1];
    CHECK(ComponentValue(custom, model.range, 180.0) == 0.0);
}

TEST_CASE("version is defined")
{
    CHECK(std::string(Version()) != "");
    CHECK(std::string(Version()) != "unknown");
}
