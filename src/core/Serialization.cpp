#include "Serialization.h"

#include <cstdio>
#include <ctime>
#include <sstream>
#include <stdexcept>

#include "Shapes.h"
#include "Version.h"

namespace giggle {

namespace {

std::string CurrentUtcTimestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

Json ParameterToJson(const FitParameter& parameter)
{
    Json json;
    json["name"] = parameter.name;
    json["value"] = parameter.value;
    json["fixed"] = parameter.fixed;
    if (parameter.lowerBound.has_value())
    {
        json["lower_bound"] = parameter.lowerBound.value();
    }
    if (parameter.upperBound.has_value())
    {
        json["upper_bound"] = parameter.upperBound.value();
    }
    return json;
}

FitParameter ParameterFromJson(const Json& json)
{
    FitParameter parameter;
    parameter.name = json.at("name").get<std::string>();
    parameter.value = json.at("value").get<double>();
    parameter.fixed = json.at("fixed").get<bool>();
    if (json.contains("lower_bound"))
    {
        parameter.lowerBound = json.at("lower_bound").get<double>();
    }
    if (json.contains("upper_bound"))
    {
        parameter.upperBound = json.at("upper_bound").get<double>();
    }
    return parameter;
}

Json ComponentToJson(const FitComponent& component)
{
    Json json;
    json["label"] = component.label;
    json["shape"] = ShapeKindName(component.shape);
    if (component.shape == ShapeKind::Custom)
    {
        json["formula"] = component.formula;
    }
    json["amplitude"] = ParameterToJson(component.amplitude);
    json["parameters"] = Json::array();
    for (const FitParameter& parameter : component.parameters)
    {
        json["parameters"].push_back(ParameterToJson(parameter));
    }
    return json;
}

FitComponent ComponentFromJson(const Json& json)
{
    FitComponent component;
    component.label = json.at("label").get<std::string>();

    std::string shapeName = json.at("shape").get<std::string>();
    std::optional<ShapeKind> shape = ShapeKindFromName(shapeName);
    if (!shape.has_value())
    {
        throw std::runtime_error("unknown shape \"" + shapeName + "\"");
    }
    component.shape = shape.value();

    if (component.shape == ShapeKind::Custom)
    {
        component.formula = json.at("formula").get<std::string>();
    }

    component.amplitude = ParameterFromJson(json.at("amplitude"));
    for (const Json& parameterJson : json.at("parameters"))
    {
        component.parameters.push_back(ParameterFromJson(parameterJson));
    }
    return component;
}

Json ProvenanceToJson(const Provenance& provenance)
{
    Json json;
    json["giggle_version"] = provenance.giggleVersion;
    json["timestamp"] = provenance.timestamp;
    json["source_file"] = provenance.sourceFile;
    json["histogram"] = provenance.histogramName;
    return json;
}

Json ValueWithErrorToJson(const ValueWithError& quantity)
{
    return { { "value", quantity.value }, { "error", quantity.error } };
}

// Joins fitted values with their parameter names from the model component.
Json ComponentResultToJson(const ComponentResult& result, const FitComponent& modelComponent)
{
    Json json;
    json["label"] = result.label;
    json["counts_in_range"] = ValueWithErrorToJson(result.counts);
    json["amplitude"] = ValueWithErrorToJson(result.amplitude);

    // Derived peak properties, when the shape defines them.
    std::optional<ValueWithError> centroid = PeakCentroid(modelComponent.shape, result);
    if (centroid.has_value())
    {
        json["centroid"] = ValueWithErrorToJson(centroid.value());
    }
    std::optional<ValueWithError> fwhm = PeakFWHM(modelComponent.shape, result);
    if (fwhm.has_value())
    {
        json["fwhm"] = ValueWithErrorToJson(fwhm.value());
    }

    json["parameters"] = Json::array();
    for (size_t i = 0; i < result.parameters.size(); ++i)
    {
        Json parameterJson;
        if (i < modelComponent.parameters.size())
        {
            parameterJson["name"] = modelComponent.parameters[i].name;
        }
        parameterJson["value"] = result.parameters[i].value;
        parameterJson["error"] = result.parameters[i].error;
        json["parameters"].push_back(parameterJson);
    }
    return json;
}

} // namespace

Provenance MakeProvenance(const std::string& sourceFile, const std::string& histogramName)
{
    Provenance provenance;
    provenance.giggleVersion = Version();
    provenance.timestamp = CurrentUtcTimestamp();
    provenance.sourceFile = sourceFile;
    provenance.histogramName = histogramName;
    return provenance;
}

Json ToJson(const FitModel& model)
{
    Json json;
    json["schema_version"] = kSchemaVersion;
    json["fit_range"] = { { "min", model.range.min }, { "max", model.range.max } };
    json["statistic"] = FitStatisticName(model.statistic);

    // How the fit is run. printLevel is a session preference (it changes no
    // result), so it deliberately stays out of the saved recipe.
    json["settings"] = {
        { "algorithm", MinimizerAlgorithmName(model.algorithm) },
        { "integrate_bins", model.integrateBins },
        { "ignore_bin_errors", model.ignoreBinErrors },
        { "count_empty_bins", model.countEmptyBins },
        { "tolerance", model.tolerance },
        { "max_iterations", model.maxIterations },
    };

    json["peaks"] = Json::array();
    for (const FitComponent& peak : model.peaks)
    {
        json["peaks"].push_back(ComponentToJson(peak));
    }

    json["background"] = Json::array();
    for (const FitComponent& component : model.background)
    {
        json["background"].push_back(ComponentToJson(component));
    }

    return json;
}

FitModel FitModelFromJson(const Json& json)
{
    FitModel model;

    model.range.min = json.at("fit_range").at("min").get<double>();
    model.range.max = json.at("fit_range").at("max").get<double>();

    std::string statisticName = json.at("statistic").get<std::string>();
    std::optional<FitStatistic> statistic = FitStatisticFromName(statisticName);
    if (!statistic.has_value())
    {
        throw std::runtime_error("unknown statistic \"" + statisticName + "\"");
    }
    model.statistic = statistic.value();

    // Optional and additive: presets written before these settings existed
    // simply keep the defaults.
    if (json.contains("settings"))
    {
        const Json& settings = json.at("settings");
        if (settings.contains("algorithm"))
        {
            std::optional<MinimizerAlgorithm> algorithm =
                MinimizerAlgorithmFromName(settings.at("algorithm").get<std::string>());
            if (algorithm.has_value())
            {
                model.algorithm = algorithm.value();
            }
        }
        if (settings.contains("integrate_bins"))
        {
            model.integrateBins = settings.at("integrate_bins").get<bool>();
        }
        if (settings.contains("ignore_bin_errors"))
        {
            model.ignoreBinErrors = settings.at("ignore_bin_errors").get<bool>();
        }
        if (settings.contains("count_empty_bins"))
        {
            model.countEmptyBins = settings.at("count_empty_bins").get<bool>();
        }
        if (settings.contains("tolerance"))
        {
            model.tolerance = settings.at("tolerance").get<double>();
        }
        if (settings.contains("max_iterations"))
        {
            model.maxIterations = settings.at("max_iterations").get<int>();
        }
    }

    for (const Json& peakJson : json.at("peaks"))
    {
        model.peaks.push_back(ComponentFromJson(peakJson));
    }
    for (const Json& componentJson : json.at("background"))
    {
        model.background.push_back(ComponentFromJson(componentJson));
    }

    return model;
}

Json MakeResultsDocument(const Provenance& provenance, const FitModel& model, const FitResult& result)
{
    Json json;
    json["schema_version"] = kSchemaVersion;
    json["provenance"] = ProvenanceToJson(provenance);
    json["fit"] = ToJson(model);

    Json resultJson;
    resultJson["converged"] = result.converged;
    resultJson["message"] = result.message;
    resultJson["chi_square"] = result.chiSquare;
    resultJson["degrees_of_freedom"] = result.degreesOfFreedom;
    resultJson["reduced_chi_square"] =
        result.degreesOfFreedom > 0 ? result.chiSquare / result.degreesOfFreedom : 0.0;

    resultJson["peaks"] = Json::array();
    for (size_t i = 0; i < result.peaks.size(); ++i)
    {
        const FitComponent& modelComponent =
            i < model.peaks.size() ? model.peaks[i] : FitComponent{};
        resultJson["peaks"].push_back(ComponentResultToJson(result.peaks[i], modelComponent));
    }

    resultJson["background"] = Json::array();
    for (size_t i = 0; i < result.background.size(); ++i)
    {
        const FitComponent& modelComponent =
            i < model.background.size() ? model.background[i] : FitComponent{};
        resultJson["background"].push_back(ComponentResultToJson(result.background[i], modelComponent));
    }

    resultJson["total_counts_in_range"] = ValueWithErrorToJson(result.totalCounts);

    Json crossCheckJson;
    crossCheckJson["performed"] = result.normSumCheck.performed;
    crossCheckJson["agreed"] = result.normSumCheck.agreed;
    crossCheckJson["detail"] = result.normSumCheck.detail;
    resultJson["normsum_cross_check"] = crossCheckJson;

    resultJson["warnings"] = result.warnings;
    resultJson["covariance"] = result.covariance;

    json["result"] = resultJson;
    return json;
}

namespace {

// One CSV cell from a double, with enough digits to round-trip.
std::string Cell(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    return buffer;
}

// Quotes a string cell if it contains characters CSV cares about.
std::string Cell(const std::string& text)
{
    if (text.find_first_of(",\"\n") == std::string::npos)
    {
        return text;
    }
    std::string quoted = "\"";
    for (char c : text)
    {
        if (c == '"')
        {
            quoted += '"';
        }
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

void AppendCsvRow(std::ostringstream& out, const ComponentResult& component,
                  const FitComponent& modelComponent, const Provenance& provenance,
                  const FitResult& result)
{
    std::optional<ValueWithError> centroid = PeakCentroid(modelComponent.shape, component);
    std::optional<ValueWithError> fwhm = PeakFWHM(modelComponent.shape, component);

    out << Cell(component.label) << ',' << ShapeKindName(modelComponent.shape) << ','
        << Cell(component.counts.value) << ',' << Cell(component.counts.error) << ','
        << (centroid.has_value() ? Cell(centroid->value) : "") << ','
        << (centroid.has_value() ? Cell(centroid->error) : "") << ','
        << (fwhm.has_value() ? Cell(fwhm->value) : "") << ','
        << (fwhm.has_value() ? Cell(fwhm->error) : "") << ','
        << Cell(component.amplitude.value) << ',' << Cell(component.amplitude.error) << ','
        << Cell(result.chiSquare) << ',' << result.degreesOfFreedom << ','
        << Cell(result.degreesOfFreedom > 0 ? result.chiSquare / result.degreesOfFreedom : 0.0) << ','
        << Cell(provenance.histogramName) << ',' << Cell(provenance.sourceFile) << ','
        << provenance.timestamp << '\n';
}

} // namespace

std::string MakeResultsCsv(const Provenance& provenance, const FitModel& model, const FitResult& result)
{
    std::ostringstream out;
    out << "component,shape,counts,counts_error,centroid,centroid_error,"
           "fwhm,fwhm_error,amplitude,amplitude_error,chi2,ndf,reduced_chi2,"
           "histogram,source_file,timestamp\n";

    for (size_t i = 0; i < result.peaks.size(); ++i)
    {
        const FitComponent& modelComponent =
            i < model.peaks.size() ? model.peaks[i] : FitComponent{};
        AppendCsvRow(out, result.peaks[i], modelComponent, provenance, result);
    }
    for (size_t i = 0; i < result.background.size(); ++i)
    {
        const FitComponent& modelComponent =
            i < model.background.size() ? model.background[i] : FitComponent{};
        AppendCsvRow(out, result.background[i], modelComponent, provenance, result);
    }

    return out.str();
}

} // namespace giggle
