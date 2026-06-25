#include "ResultsPanel.h"

#include <cstdio>
#include <optional>
#include <string>

#include "imgui.h"

#include "core/Shapes.h"
#include "ui/fonts/IconsFontAwesome5.h"

namespace giggle {

namespace {

// "12345.6 +- 78.9", or "-" for an absent quantity.
std::string Formatted(const std::optional<ValueWithError>& quantity)
{
    if (!quantity.has_value())
    {
        return "-";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g +- %.3g", quantity->value, quantity->error);
    return buffer;
}

} // namespace

ResultsAction ResultsPanel::Draw(const FitResult* result, const HistogramData* histogram,
                                 const FitModel* model, const Theme& theme, ImFont* monoFont)
{
    ResultsAction action;
    m_histogramForUnits = histogram;

    if (ImGui::Begin(Title))
    {
        // The raw data counts in the fit range are always available, fit
        // or no fit: the physical number the model total must account for.
        bool haveRange = histogram != nullptr && model != nullptr
                         && model->range.max > model->range.min;
        if (haveRange)
        {
            RegionStats data = AnalyzeRange(*histogram, model->range);
            ImGui::PushFont(monoFont, 0.0f);
            ImGui::Text("data in range   N = %10.6g +- %-8.4g   centroid = %.6g   rms = %.4g",
                        data.counts.value, data.counts.error, data.centroid, data.rms);
            if (result != nullptr)
            {
                ImGui::Text("model total     N = %10.6g +- %-8.4g",
                            result->totalCounts.value, result->totalCounts.error);
            }
            ImGui::PopFont();
            ImGui::Separator();
        }

        if (result == nullptr)
        {
            ImGui::TextDisabled("No fit yet.");
        }
        else
        {
            DrawSummaryLine(*result, theme, monoFont);
            if (model != nullptr)
            {
                // The method, on the record next to the numbers.
                const char* algorithm = "MIGRAD";
                switch (model->algorithm)
                {
                    case MinimizerAlgorithm::Migrad:      algorithm = "MIGRAD"; break;
                    case MinimizerAlgorithm::Simplex:     algorithm = "SIMPLEX"; break;
                    case MinimizerAlgorithm::Scan:        algorithm = "SCAN"; break;
                    case MinimizerAlgorithm::Combination: algorithm = "Combination"; break;
                }
                const char* statistic = model->statistic == FitStatistic::ChiSquare
                                            ? "chi-square"
                                            : "Poisson likelihood";
                ImGui::TextDisabled("Minuit2 \xc2\xb7 %s \xc2\xb7 %s", algorithm, statistic);
            }
            ImGui::Separator();
            if (model != nullptr)
            {
                DrawComponentsTable(*result, *model, monoFont);
            }
            DrawWarnings(*result, theme);

            if (result->converged)
            {
                ImGui::Spacing();
                if (ImGui::Button(ICON_FA_SAVE "  Save JSON..."))
                {
                    action.saveJsonRequested = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_SAVE "  Save CSV..."))
                {
                    action.saveCsvRequested = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_COPY "  Copy CSV"))
                {
                    action.copyCsvRequested = true;
                }
            }
        }
    }
    ImGui::End();

    return action;
}

void ResultsPanel::DrawSummaryLine(const FitResult& result, const Theme& theme, ImFont* monoFont)
{
    if (result.converged)
    {
        ImGui::TextColored(theme.statusGood, "Converged");
    }
    else
    {
        ImGui::TextColored(theme.statusError, "Failed: %s", result.message.c_str());
    }

    ImGui::SameLine(0.0f, 30.0f);
    ImGui::PushFont(monoFont, 0.0f);
    double reduced = result.degreesOfFreedom > 0
                         ? result.chiSquare / result.degreesOfFreedom
                         : 0.0;
    ImGui::Text("chi2/ndf = %.4g / %d = %.3f", result.chiSquare, result.degreesOfFreedom, reduced);
    ImGui::PopFont();

    // The independent verification of the counts.
    if (result.converged)
    {
        ImGui::SameLine(0.0f, 30.0f);
        const CrossCheck& check = result.normSumCheck;
        if (!check.performed)
        {
            ImGui::TextDisabled("counts not independently verified: %s", check.detail.c_str());
        }
        else if (check.agreed)
        {
            ImGui::TextColored(theme.statusGood, "counts independently verified (%s)",
                               check.detail.c_str());
        }
        else
        {
            ImGui::TextColored(theme.statusError, "VERIFICATION FAILED (%s) - do not trust these counts",
                               check.detail.c_str());
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "After every converged fit, GIGGLE re-fits the same data a second,\n"
                "mathematically independent way (ROOT's TF1NormSum, where each\n"
                "component's counts are themselves fit parameters) and compares the\n"
                "counts and uncertainties from both routes.\n"
                "\n"
                "Agreement means two independent methods give the same numbers.\n"
                "Disagreement means the fit is unstable: inspect it before using it.");
        }
    }
}

void ResultsPanel::DrawComponentsTable(const FitResult& result, const FitModel& model,
                                       ImFont* monoFont)
{
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                            | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("components", 4, flags))
    {
        return;
    }

    ImGui::TableSetupColumn("component");
    ImGui::TableSetupColumn("counts in range");
    ImGui::TableSetupColumn("centroid");
    ImGui::TableSetupColumn("FWHM");
    ImGui::TableHeadersRow();

    auto drawRow = [&](const ComponentResult& component, const FitComponent& modelComponent) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(component.label.c_str());

        ImGui::PushFont(monoFont, 0.0f);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Formatted(component.counts).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Formatted(PeakCentroid(modelComponent.shape, component)).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Formatted(PeakFWHM(modelComponent.shape, component)).c_str());
        ImGui::PopFont();
    };

    for (size_t i = 0; i < result.peaks.size() && i < model.peaks.size(); ++i)
    {
        drawRow(result.peaks[i], model.peaks[i]);
    }
    for (size_t i = 0; i < result.background.size() && i < model.background.size(); ++i)
    {
        drawRow(result.background[i], model.background[i]);
    }

    ImGui::EndTable();

    // The raw fitted parameters, for the record. Amplitudes show in the
    // same plot units as the Fit Model panel (height/level in counts);
    // exports keep the raw density value.
    if (ImGui::TreeNode("All parameters"))
    {
        ImGui::PushFont(monoFont, 0.0f);
        auto listComponent = [&](const ComponentResult& component, const FitComponent& modelComponent,
                                 bool isPeak) {
            double position = m_histogramForUnits != nullptr ? m_histogramForUnits->XMin() : 0.0;
            for (const FitParameter& parameter : modelComponent.parameters)
            {
                if (parameter.name == "mean")
                {
                    position = parameter.value;
                    break;
                }
            }
            bool custom = modelComponent.shape == ShapeKind::Custom;
            double scale = custom || m_histogramForUnits == nullptr
                               ? 1.0
                               : BinWidthAt(*m_histogramForUnits, position);
            const char* amplitudeLabel = custom ? "amplitude" : (isPeak ? "height" : "level");

            ImGui::Text("%-12s  %-9s = %10.6g +- %-8.3g", component.label.c_str(), amplitudeLabel,
                        component.amplitude.value * scale, component.amplitude.error * scale);
            for (size_t j = 0; j < component.parameters.size(); ++j)
            {
                const char* name = j < modelComponent.parameters.size()
                                       ? modelComponent.parameters[j].name.c_str()
                                       : "?";
                ImGui::Text("%-12s  %-9s = %10.6g +- %-8.3g", "", name,
                            component.parameters[j].value, component.parameters[j].error);
            }
        };
        for (size_t i = 0; i < result.peaks.size() && i < model.peaks.size(); ++i)
        {
            listComponent(result.peaks[i], model.peaks[i], true);
        }
        for (size_t i = 0; i < result.background.size() && i < model.background.size(); ++i)
        {
            listComponent(result.background[i], model.background[i], false);
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }
}

void ResultsPanel::DrawWarnings(const FitResult& result, const Theme& theme)
{
    for (const std::string& warning : result.warnings)
    {
        ImGui::TextColored(theme.statusWarning, "! %s", warning.c_str());
    }
}

} // namespace giggle
