#include "ResultsPanel.h"

#include "imgui.h"

#include "core/Shapes.h"

namespace giggle {

void ResultsPanel::Draw(const FitResult* result, const HistogramData* histogram,
                        const FitModel* model, const Theme& theme, ImFont* monoFont)
{
    if (ImGui::Begin(Title))
    {
        // The raw data counts in the fit range are always available, fit
        // or no fit: the physical number the model total must account for.
        bool haveRange = histogram != nullptr && model != nullptr
                         && model->range.max > model->range.min;
        if (haveRange)
        {
            ValueWithError data = CountsInRange(*histogram, model->range);
            ImGui::PushFont(monoFont, 0.0f);
            ImGui::Text("data in range   N = %10.6g +- %-8.4g", data.value, data.error);
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
            if (result->converged)
            {
                ImGui::TextColored(theme.statusGood, "Converged");
            }
            else
            {
                ImGui::TextColored(theme.statusError, "Failed: %s", result->message.c_str());
            }

            ImGui::SameLine(0.0f, 30.0f);
            ImGui::PushFont(monoFont, 0.0f);
            ImGui::Text("chi2/ndf = %.4g / %d", result->chiSquare, result->degreesOfFreedom);
            ImGui::PopFont();

            // The independent TF1NormSum re-fit of the same data.
            if (result->converged)
            {
                ImGui::SameLine(0.0f, 30.0f);
                const CrossCheck& check = result->normSumCheck;
                if (!check.performed)
                {
                    ImGui::TextDisabled("cross-check %s", check.detail.c_str());
                }
                else if (check.agreed)
                {
                    ImGui::TextColored(theme.statusGood, "cross-check passed (%s)",
                                       check.detail.c_str());
                }
                else
                {
                    ImGui::TextColored(theme.statusError, "CROSS-CHECK FAILED (%s)",
                                       check.detail.c_str());
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Every converged fit is repeated through ROOT's\n"
                                      "TF1NormSum and the counts compared.");
                }
            }

            ImGui::Separator();

            ImGui::PushFont(monoFont, 0.0f);
            for (const ComponentResult& peak : result->peaks)
            {
                ImGui::Text("%-12s  N = %10.6g +- %-8.4g", peak.label.c_str(),
                            peak.counts.value, peak.counts.error);
            }
            for (const ComponentResult& background : result->background)
            {
                ImGui::Text("%-12s  N = %10.6g +- %-8.4g", background.label.c_str(),
                            background.counts.value, background.counts.error);
            }
            ImGui::PopFont();
        }
    }
    ImGui::End();
}

} // namespace giggle
