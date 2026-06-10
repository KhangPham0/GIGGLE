#include "Serialization.h"

#include <ctime>
#include <stdexcept>

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
    json["yield"] = ParameterToJson(component.yield);
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

    component.yield = ParameterFromJson(json.at("yield"));
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

// Joins fitted values with their parameter names from the model component.
Json ComponentResultToJson(const ComponentResult& result, const FitComponent& modelComponent)
{
    Json json;
    json["label"] = result.label;
    json["yield"] = { { "value", result.yield.value }, { "error", result.yield.error } };
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

    resultJson["covariance"] = result.covariance;

    json["result"] = resultJson;
    return json;
}

} // namespace giggle
