#include "FitModelPanel.h"

#include <cmath>
#include <string>

#include "imgui.h"

namespace giggle {

namespace {

// Shapes offered for the background section in this version. Peaks are
// Gaussian-only until the palette grows.
const ShapeKind kBackgroundShapes[] = {
    ShapeKind::Constant,
    ShapeKind::Linear,
    ShapeKind::Quadratic,
};

// "Peak <n>" with n above every number already in use.
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

FitComponent MakeDefaultPeak(const FitRange& range, const std::string& label)
{
    FitComponent peak;
    peak.label = label;
    peak.shape = ShapeKind::Gaussian;
    peak.yield = { "yield", 100.0, false, 0.0, std::nullopt };
    peak.parameters = {
        { "mean", (range.min + range.max) / 2.0, false, std::nullopt, std::nullopt },
        { "sigma", (range.max - range.min) / 40.0, false, std::nullopt, std::nullopt },
    };
    return peak;
}

FitComponent MakeDefaultBackground(ShapeKind shape, const FitRange& range)
{
    FitComponent background;
    background.label = "Background";
    background.shape = shape;
    background.yield = { "yield", 100.0, false, 0.0, std::nullopt };
    for (const std::string& name : ShapeParameterNames(shape))
    {
        background.parameters.push_back({ name, 0.0, false, std::nullopt, std::nullopt });
    }
    (void)range;
    return background;
}

// A drag speed proportional to the value, so both small and large numbers
// are adjustable. Double-click the field to type a value directly.
float DragSpeedFor(double value)
{
    double magnitude = std::abs(value);
    return magnitude > 1e-12 ? static_cast<float>(magnitude * 0.005) : 0.01f;
}

} // namespace

void FitModelPanel::Draw(FitModel& model, bool enabled, ImFont* monoFont)
{
    if (ImGui::Begin(Title))
    {
        if (!enabled)
        {
            ImGui::TextDisabled("Open a histogram to build a fit.");
        }
        else
        {
            DrawRangeSection(model, monoFont);
            DrawPeaksSection(model, monoFont);
            DrawBackgroundSection(model, monoFont);
            DrawStatisticSection(model);
        }
    }
    ImGui::End();
}

void FitModelPanel::DrawRangeSection(FitModel& model, ImFont* monoFont)
{
    if (!ImGui::CollapsingHeader("Fit Range", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    ImGui::PushFont(monoFont, 0.0f);
    float fieldWidth = ImGui::GetContentRegionAvail().x * 0.4f;

    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::DragScalar("##range_min", ImGuiDataType_Double, &model.range.min,
                      DragSpeedFor(model.range.max - model.range.min), nullptr, nullptr, "%.6g");
    ImGui::SameLine();
    ImGui::TextUnformatted("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::DragScalar("##range_max", ImGuiDataType_Double, &model.range.max,
                      DragSpeedFor(model.range.max - model.range.min), nullptr, nullptr, "%.6g");
    ImGui::PopFont();

    if (model.range.max <= model.range.min)
    {
        ImGui::TextColored(ImVec4(0.90f, 0.71f, 0.33f, 1.0f), "Range is empty.");
    }
}

void FitModelPanel::DrawPeaksSection(FitModel& model, ImFont* monoFont)
{
    if (!ImGui::CollapsingHeader("Peaks", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    if (model.peaks.empty())
    {
        ImGui::TextDisabled("No peaks.");
    }

    int removeIndex = -1;
    for (size_t i = 0; i < model.peaks.size(); ++i)
    {
        FitComponent& peak = model.peaks[i];
        ImGui::PushID(static_cast<int>(i));

        ImGui::SeparatorText(peak.label.c_str());
        if (DrawParameterRow(peak.yield, "yield", monoFont)) {}
        for (FitParameter& parameter : peak.parameters)
        {
            DrawParameterRow(parameter, parameter.name.c_str(), monoFont);
        }
        if (ImGui::SmallButton("Remove"))
        {
            removeIndex = static_cast<int>(i);
        }

        ImGui::PopID();
    }
    if (removeIndex >= 0)
    {
        model.peaks.erase(model.peaks.begin() + removeIndex);
    }

    ImGui::Spacing();
    if (ImGui::Button("Add Peak", ImVec2(-1.0f, 0.0f)))
    {
        model.peaks.push_back(MakeDefaultPeak(model.range, NextPeakLabel(model.peaks)));
    }
}

void FitModelPanel::DrawBackgroundSection(FitModel& model, ImFont* monoFont)
{
    if (!ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    // This version manages a single background component; "None" removes it.
    const char* currentName =
        model.background.empty() ? "None" : ShapeKindName(model.background.front().shape);

    if (ImGui::BeginCombo("##background_shape", currentName))
    {
        if (ImGui::Selectable("None", model.background.empty()))
        {
            model.background.clear();
        }
        for (ShapeKind shape : kBackgroundShapes)
        {
            bool selected = !model.background.empty() && model.background.front().shape == shape;
            if (ImGui::Selectable(ShapeKindName(shape), selected) && !selected)
            {
                model.background.clear();
                model.background.push_back(MakeDefaultBackground(shape, model.range));
            }
        }
        ImGui::EndCombo();
    }

    if (!model.background.empty())
    {
        FitComponent& background = model.background.front();
        ImGui::PushID("background");
        DrawParameterRow(background.yield, "yield", monoFont);
        for (FitParameter& parameter : background.parameters)
        {
            DrawParameterRow(parameter, parameter.name.c_str(), monoFont);
        }
        ImGui::PopID();
    }
}

void FitModelPanel::DrawStatisticSection(FitModel& model)
{
    if (!ImGui::CollapsingHeader("Fit Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    const char* names[] = { "Chi-square", "Poisson likelihood" };
    int current = model.statistic == FitStatistic::ChiSquare ? 0 : 1;
    if (ImGui::Combo("statistic", &current, names, 2))
    {
        model.statistic = current == 0 ? FitStatistic::ChiSquare : FitStatistic::PoissonLikelihood;
    }
}

bool FitModelPanel::DrawParameterRow(FitParameter& parameter, const char* id, ImFont* monoFont)
{
    ImGui::PushID(id);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(id);

    // Right-align the fix checkbox; the value field takes the middle.
    float checkboxWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    float valueStart = ImGui::GetContentRegionAvail().x * 0.35f;

    ImGui::SameLine(valueStart);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - checkboxWidth - 40.0f);
    ImGui::PushFont(monoFont, 0.0f);
    bool changed = ImGui::DragScalar("##value", ImGuiDataType_Double, &parameter.value,
                                     DragSpeedFor(parameter.value), nullptr, nullptr, "%.6g");
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::Checkbox("fix", &parameter.fixed);

    ImGui::PopID();
    return changed;
}

} // namespace giggle
