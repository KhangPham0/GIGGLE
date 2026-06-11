#include "FitModelPanel.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "imgui.h"

#include "core/Shapes.h"

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

// The tallest bin within the range: the natural scale for new amplitudes.
double MaxCountsInRange(const HistogramData& histogram, const FitRange& range)
{
    double best = 0.0;
    for (int bin = 0; bin < histogram.BinCount(); ++bin)
    {
        double center = 0.5 * (histogram.binEdges[bin] + histogram.binEdges[bin + 1]);
        if (center >= range.min && center <= range.max)
        {
            best = std::max(best, histogram.counts[bin]);
        }
    }
    return best;
}

FitComponent MakeDefaultPeak(const FitRange& range, const HistogramData& histogram,
                             const std::string& label)
{
    double mean = (range.min + range.max) / 2.0;
    double height = std::max(MaxCountsInRange(histogram, range) * 0.5, 1.0);

    FitComponent peak;
    peak.label = label;
    peak.shape = ShapeKind::Gaussian;
    peak.amplitude = { "amplitude", height / BinWidthAt(histogram, mean), false, 0.0, std::nullopt };
    peak.parameters = {
        { "mean", mean, false, std::nullopt, std::nullopt },
        { "sigma", (range.max - range.min) / 40.0, false, std::nullopt, std::nullopt },
    };
    return peak;
}

FitComponent MakeDefaultBackground(ShapeKind shape, const FitRange& range,
                                   const HistogramData& histogram)
{
    double center = (range.min + range.max) / 2.0;
    double level = std::max(MaxCountsInRange(histogram, range) * 0.2, 1.0);

    FitComponent background;
    background.label = "Background";
    background.shape = shape;
    background.amplitude = { "amplitude", level / BinWidthAt(histogram, center), false, 0.0, std::nullopt };
    for (const std::string& name : ShapeParameterNames(shape))
    {
        background.parameters.push_back({ name, 0.0, false, std::nullopt, std::nullopt });
    }
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

FitPanelAction FitModelPanel::Draw(FitModel& model, const HistogramData* histogram,
                                   bool fitRunning, bool canRevert, const Theme& theme,
                                   ImFont* monoFont)
{
    FitPanelAction action;

    if (ImGui::Begin(Title))
    {
        if (histogram == nullptr)
        {
            ImGui::TextDisabled("Open a histogram to build a fit.");
        }
        else
        {
            DrawRangeSection(model, *histogram, monoFont);
            DrawPeaksSection(model, *histogram, monoFont);
            DrawBackgroundSection(model, *histogram, monoFont);
            DrawStatisticSection(model);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool fittable = (!model.peaks.empty() || !model.background.empty())
                            && model.range.max > model.range.min;

            ImGui::BeginDisabled(fitRunning || !fittable);
            ImGui::PushStyleColor(ImGuiCol_Button, theme.accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.accentHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.accentActive);
            ImGui::PushStyleColor(ImGuiCol_Text, theme.windowBackground);
            if (ImGui::Button(fitRunning ? "Fitting..." : "Fit", ImVec2(-1.0f, 0.0f)))
            {
                action.fitRequested = true;
            }
            ImGui::PopStyleColor(4);
            ImGui::EndDisabled();

            if (canRevert)
            {
                if (ImGui::Button("Revert to pre-fit", ImVec2(-1.0f, 0.0f)))
                {
                    action.revertRequested = true;
                }
            }
        }
    }
    ImGui::End();

    return action;
}

void FitModelPanel::DrawRangeSection(FitModel& model, const HistogramData& histogram,
                                     ImFont* monoFont)
{
    if (!ImGui::CollapsingHeader("Fit Range", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    ImGui::PushFont(monoFont, 0.0f);
    float fieldWidth = ImGui::GetContentRegionAvail().x * 0.4f;

    bool edited = false;
    ImGui::SetNextItemWidth(fieldWidth);
    edited |= ImGui::DragScalar("##range_min", ImGuiDataType_Double, &model.range.min,
                                DragSpeedFor(model.range.max - model.range.min), nullptr, nullptr, "%.6g");
    ImGui::SameLine();
    ImGui::TextUnformatted("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fieldWidth);
    edited |= ImGui::DragScalar("##range_max", ImGuiDataType_Double, &model.range.max,
                                DragSpeedFor(model.range.max - model.range.min), nullptr, nullptr, "%.6g");
    ImGui::PopFont();

    // The fit works on whole bins, so the range the user sees is the range
    // the fit will really use.
    if (edited)
    {
        model.range = SnapRangeToBinEdges(histogram, model.range);
    }

    if (model.range.max <= model.range.min)
    {
        ImGui::TextColored(ImVec4(0.90f, 0.71f, 0.33f, 1.0f), "Range is empty.");
    }
}

void FitModelPanel::DrawPeaksSection(FitModel& model, const HistogramData& histogram,
                                     ImFont* monoFont)
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
        DrawAmplitudeRow(peak, histogram, "height", monoFont);
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
        model.peaks.push_back(MakeDefaultPeak(model.range, histogram, NextPeakLabel(model.peaks)));
    }
}

void FitModelPanel::DrawBackgroundSection(FitModel& model, const HistogramData& histogram,
                                          ImFont* monoFont)
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
                model.background.push_back(MakeDefaultBackground(shape, model.range, histogram));
            }
        }
        ImGui::EndCombo();
    }

    if (!model.background.empty())
    {
        FitComponent& background = model.background.front();
        ImGui::PushID("background");
        DrawAmplitudeRow(background, histogram, "level", monoFont);
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

// The amplitude is a real fit parameter, displayed in plot units: the
// component's counts per bin at its reference point ("height" of a peak,
// "level" of a background). The conversion is the local bin width -- a
// constant -- so the fix checkbox and future bounds act on exactly what
// the user sees.
void FitModelPanel::DrawAmplitudeRow(FitComponent& component, const HistogramData& histogram,
                                     const char* label, ImFont* monoFont)
{
    // The reference position: "mean" for peaks; backgrounds use any bin
    // width (uniform in practice), so the first bin is fine.
    double position = histogram.XMin();
    for (const FitParameter& parameter : component.parameters)
    {
        if (parameter.name == "mean")
        {
            position = parameter.value;
            break;
        }
    }
    double binWidth = BinWidthAt(histogram, position);

    double display = component.amplitude.value * binWidth;

    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("In counts, as read off the plot.");
    }

    float checkboxWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - checkboxWidth - 40.0f);
    ImGui::PushFont(monoFont, 0.0f);
    if (ImGui::DragScalar("##value", ImGuiDataType_Double, &display,
                          DragSpeedFor(display), nullptr, nullptr, "%.6g"))
    {
        component.amplitude.value = display / binWidth;
    }
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::Checkbox("fix", &component.amplitude.fixed);

    ImGui::PopID();
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
