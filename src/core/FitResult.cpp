#include "FitResult.h"

#include "FitModel.h"

namespace giggle {

namespace {

void ApplyComponentResults(const std::vector<ComponentResult>& results,
                           std::vector<FitComponent>& components)
{
    for (size_t i = 0; i < results.size() && i < components.size(); ++i)
    {
        components[i].amplitude.value = results[i].amplitude.value;
        for (size_t j = 0; j < results[i].parameters.size() && j < components[i].parameters.size(); ++j)
        {
            components[i].parameters[j].value = results[i].parameters[j].value;
        }
    }
}

} // namespace

void ApplyFitResult(const FitResult& result, FitModel& model)
{
    ApplyComponentResults(result.peaks, model.peaks);
    ApplyComponentResults(result.background, model.background);
}

} // namespace giggle
