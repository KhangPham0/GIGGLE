#include "FitModelPanel.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

#include "imgui.h"
#include "imgui_stdlib.h"

#include "core/Shapes.h"
#include "ui/Widgets.h"
#include "ui/fonts/IconsFontAwesome5.h"

namespace giggle {

namespace {

const ShapeKind kPeakShapes[] = {
    ShapeKind::Gaussian,
    ShapeKind::GaussianTail,
    ShapeKind::Lorentzian,
    ShapeKind::Voigt,
    ShapeKind::CrystalBall,
    ShapeKind::Landau,
    ShapeKind::Custom,
};

const ShapeKind kBackgroundShapes[] = {
    ShapeKind::Constant,
    ShapeKind::Linear,
    ShapeKind::Quadratic,
    ShapeKind::Exponential,
    ShapeKind::Gaussian,
    ShapeKind::Step,
    ShapeKind::Custom,
};

// A sensible default for a shape parameter created out of thin air.
double DefaultParameterValue(const std::string& name, const FitRange& range)
{
    if (name == "mean" || name == "edge")
    {
        return (range.min + range.max) / 2.0;
    }
    if (name == "sigma")
    {
        return (range.max - range.min) / 12.0;
    }
    if (name == "scale")
    {
        return (range.max - range.min) / 20.0; // landau is sharper than a gaussian
    }
    if (name == "gamma")
    {
        return (range.max - range.min) / 24.0;
    }
    if (name == "tail_fraction")
    {
        return 0.2; // a modest tail, as gf3 defaults suggest
    }
    if (name == "tail_length")
    {
        return (range.max - range.min) / 30.0;
    }
    if (name == "width")
    {
        return (range.max - range.min) / 100.0;
    }
    if (name == "alpha")
    {
        return 1.5; // tail begins 1.5 sigma below the mean
    }
    if (name == "n")
    {
        return 3.0; // power-law exponent
    }
    return 0.0;
}

// Changes a component's shape, carrying over what translates: the mean,
// and a comparable width. Custom shapes keep their formula and wait for
// Apply to define their parameters.
void ConvertComponentShape(FitComponent& component, ShapeKind to, const FitRange& range)
{
    if (component.shape == to)
    {
        return;
    }

    auto currentValue = [&component](const char* name) -> std::optional<double> {
        for (const FitParameter& parameter : component.parameters)
        {
            if (parameter.name == name)
            {
                return parameter.value;
            }
        }
        return std::nullopt;
    };

    std::optional<double> mean = currentValue("mean");
    // A sigma-like width: sigma directly, or gamma rescaled so the FWHM
    // roughly carries over (gamma = 1.177 sigma at equal FWHM).
    std::optional<double> width = currentValue("sigma");
    if (!width.has_value())
    {
        std::optional<double> gamma = currentValue("gamma");
        if (gamma.has_value())
        {
            width = gamma.value() / 1.177410023;
        }
    }
    if (!width.has_value())
    {
        width = currentValue("scale"); // carry the landau scale across as a width
    }

    component.shape = to;
    component.parameters.clear();
    if (to == ShapeKind::Custom)
    {
        return; // parameters appear when the formula is applied
    }
    component.formula.clear();

    for (const std::string& name : ShapeParameterNames(to))
    {
        double value = DefaultParameterValue(name, range);
        if (name == "mean" && mean.has_value())
        {
            value = mean.value();
        }
        if (name == "sigma" && width.has_value())
        {
            value = width.value();
        }
        if (name == "gamma" && width.has_value())
        {
            value = to == ShapeKind::Voigt ? 0.5 * width.value() : 1.177410023 * width.value();
        }
        if (name == "tail_length" && width.has_value())
        {
            value = width.value(); // a tail about one sigma long to start
        }
        if (name == "scale" && width.has_value())
        {
            value = width.value(); // landau scale carries the gaussian width
        }

        FitParameter parameter{ name, value, false, std::nullopt, std::nullopt };
        if (name == "tail_fraction")
        {
            parameter.lowerBound = 0.0; // a fraction stays one
            parameter.upperBound = 1.0;
        }
        if (name == "tail_length" || name == "width" || name == "scale"
            || name == "sigma" || name == "gamma")
        {
            parameter.lowerBound = 0.0;
        }
        if (name == "alpha")
        {
            parameter.lowerBound = 0.0; // tail start stays on the low side
        }
        if (name == "n")
        {
            parameter.lowerBound = 1.0; // power-law exponent above 1
        }
        component.parameters.push_back(parameter);
    }
}

// The shape selector for one component. Returns true when it changed.
bool DrawShapeCombo(FitComponent& component, const ShapeKind* shapes, int shapeCount,
                    const FitRange& range)
{
    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    if (ImGui::BeginCombo("##shape", ShapeKindName(component.shape)))
    {
        for (int i = 0; i < shapeCount; ++i)
        {
            bool selected = component.shape == shapes[i];
            if (ImGui::Selectable(ShapeKindName(shapes[i]), selected) && !selected)
            {
                ConvertComponentShape(component, shapes[i], range);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
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
        // Slopes and curvatures start flat; a Gaussian background starts
        // centered with a generous width.
        double value = name == "slope" || name == "curvature"
                           ? 0.0
                           : DefaultParameterValue(name, range);
        FitParameter parameter{ name, value, false, std::nullopt, std::nullopt };
        if (name == "width" || name == "sigma" || name == "gamma")
        {
            parameter.lowerBound = 0.0; // widths stay non-negative
        }
        background.parameters.push_back(parameter);
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

// One optional bound inside the bounds popup: a checkbox to enable it and
// a field to set it, working in display units (bound = display / scale).
void DrawBoundField(const char* label, std::optional<double>& bound, double scale,
                    double defaultDisplayValue)
{
    ImGui::PushID(label);

    bool enabled = bound.has_value();
    if (ImGui::Checkbox(label, &enabled))
    {
        if (enabled)
        {
            bound = defaultDisplayValue / scale;
        }
        else
        {
            bound.reset();
        }
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::BeginDisabled(!enabled);
    double display = bound.has_value() ? bound.value() * scale : 0.0;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragScalar("##bound", ImGuiDataType_Double, &display,
                          DragSpeedFor(display), nullptr, nullptr, "%.6g")
        && bound.has_value())
    {
        bound = display / scale;
    }
    ImGui::EndDisabled();

    ImGui::PopID();
}

// The right-click popup on a value field, plus the hover hint showing the
// bounds. Attached to the last drawn item. `scale` converts stored values
// to display units (the bin width for amplitudes, 1 otherwise).
void DrawBoundsPopup(FitParameter& parameter, double scale)
{
    if (ImGui::IsItemHovered())
    {
        if (parameter.lowerBound.has_value() || parameter.upperBound.has_value())
        {
            char text[96];
            std::snprintf(text, sizeof(text), "bounds: %s%.6g, %.6g%s",
                          parameter.lowerBound.has_value() ? "[" : "(",
                          parameter.lowerBound.has_value() ? parameter.lowerBound.value() * scale
                                                           : -INFINITY,
                          parameter.upperBound.has_value() ? parameter.upperBound.value() * scale
                                                           : INFINITY,
                          parameter.upperBound.has_value() ? "]" : ")");
            ImGui::SetTooltip("%s", text);
        }
        else
        {
            // The hint, where you'd actually use it, instead of a panel line.
            ImGui::SetTooltip("right-click to set bounds");
        }
    }

    if (ImGui::BeginPopupContextItem("bounds"))
    {
        double value = parameter.value * scale;
        DrawBoundField("lower bound", parameter.lowerBound, scale, value);
        DrawBoundField("upper bound", parameter.upperBound, scale, value);
        ImGui::EndPopup();
    }
}

// The drag limits enforcing bounds while dragging. ImGui wants concrete
// numbers, so missing sides fall back to huge ones.
struct DragLimits
{
    double minimum;
    double maximum;
    const double* minimumPointer;
    const double* maximumPointer;
};

DragLimits LimitsFor(const FitParameter& parameter, double scale)
{
    DragLimits limits;
    limits.minimum = parameter.lowerBound.has_value() ? parameter.lowerBound.value() * scale : -1e300;
    limits.maximum = parameter.upperBound.has_value() ? parameter.upperBound.value() * scale : 1e300;
    bool bounded = parameter.lowerBound.has_value() || parameter.upperBound.has_value();
    limits.minimumPointer = bounded ? &limits.minimum : nullptr;
    limits.maximumPointer = bounded ? &limits.maximum : nullptr;
    return limits;
}

// A dimmed "(?)" after the previous item that explains a setting on hover --
// the discoverable replacement for the old fit-flags text box.
void HelpMarker(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", text);
    }
}

// A small dimmed lock after a fixed parameter's label, so a glance shows
// which values are held. The value field stays editable -- fixing only
// disables the fit and the on-plot handle, not typing here.
void FixedLockGlyph(bool fixed)
{
    if (!fixed)
    {
        return;
    }
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextDisabled(ICON_FA_LOCK);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Fixed: held in the fit and not draggable.\n"
                          "You can still set its value here.");
    }
}

} // namespace

FitPanelAction FitModelPanel::Draw(FitModel& model, const HistogramData* histogram,
                                   bool fitRunning, bool canRevert, const Theme& theme,
                                   ImFont* monoFont, const FormulaValidator& validator)
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
            DrawRangeSection(model, *histogram, theme, monoFont);
            DrawPeaksSection(model, *histogram, monoFont, theme, validator);
            DrawBackgroundSection(model, *histogram, monoFont, theme, validator);
            DrawStatisticSection(model, theme);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Presets: the whole model as a JSON file, reusable across the
            // histograms of a measurement.
            float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
            if (ImGui::Button(ICON_FA_SAVE "  Save preset...", ImVec2(halfWidth, 0.0f)))
            {
                action.savePresetRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FILE_IMPORT "  Load preset...", ImVec2(halfWidth, 0.0f)))
            {
                action.loadPresetRequested = true;
            }

            bool fittable = (!model.peaks.empty() || !model.background.empty())
                            && model.range.max > model.range.min;

            ImGui::BeginDisabled(fitRunning || !fittable);
            ImGui::PushStyleColor(ImGuiCol_Button, theme.accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.accentHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.accentActive);
            ImGui::PushStyleColor(ImGuiCol_Text, theme.windowBackground);
            if (ImGui::Button(fitRunning ? ICON_FA_HOURGLASS_HALF "  Fitting..." : ICON_FA_PLAY "  Fit",
                              ImVec2(-1.0f, 0.0f)))
            {
                action.fitRequested = true;
            }
            ImGui::PopStyleColor(4);
            ImGui::EndDisabled();

            if (canRevert)
            {
                if (ImGui::Button(ICON_FA_UNDO "  Revert to pre-fit", ImVec2(-1.0f, 0.0f)))
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
                                     const Theme& theme, ImFont* monoFont)
{
    if (!SectionHeader("Fit Range", theme, ImGuiTreeNodeFlags_DefaultOpen))
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
                                     ImFont* monoFont, const Theme& theme,
                                     const FormulaValidator& validator)
{
    if (!SectionHeader("Peaks", theme, ImGuiTreeNodeFlags_DefaultOpen))
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

        SubHeader(peak.label.c_str(), theme);
        DrawShapeCombo(peak, kPeakShapes, static_cast<int>(std::size(kPeakShapes)), model.range);
        if (peak.shape == ShapeKind::Custom)
        {
            DrawFormulaEditor(peak, theme, validator);
        }
        DrawAmplitudeRow(peak, histogram, peak.shape == ShapeKind::Custom ? "amplitude" : "height",
                         monoFont);
        for (FitParameter& parameter : peak.parameters)
        {
            DrawParameterRow(parameter, parameter.name.c_str(), monoFont);
        }
        if (ImGui::SmallButton(ICON_FA_TRASH "  Remove"))
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
    if (ImGui::Button(ICON_FA_PLUS "  Add Peak", ImVec2(-1.0f, 0.0f)))
    {
        FitComponent peak = SuggestGaussianPeak(histogram, model.range,
                                                (model.range.min + model.range.max) / 2.0);
        peak.label = NextPeakLabel(model.peaks);
        model.peaks.push_back(peak);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Tip: hold P and click a peak on the plot, or right-click it.");
    }
}

void FitModelPanel::DrawBackgroundSection(FitModel& model, const HistogramData& histogram,
                                          ImFont* monoFont, const Theme& theme,
                                          const FormulaValidator& validator)
{
    if (!SectionHeader("Background", theme, ImGuiTreeNodeFlags_DefaultOpen))
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
        if (background.shape == ShapeKind::Custom)
        {
            DrawFormulaEditor(background, theme, validator);
        }
        DrawAmplitudeRow(background, histogram,
                         background.shape == ShapeKind::Custom ? "amplitude" : "level", monoFont);
        for (FitParameter& parameter : background.parameters)
        {
            DrawParameterRow(parameter, parameter.name.c_str(), monoFont);
        }
        ImGui::PopID();
    }
}

// The formula field with an Apply button. Applying validates the formula
// and rebuilds the component's parameter list to match it; values of
// parameters that keep their position survive.
void FitModelPanel::DrawFormulaEditor(FitComponent& component, const Theme& theme,
                                      const FormulaValidator& validator)
{
    ImGui::PushID("formula");

    float buttonWidth = ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 4;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth
                            - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##text", "formula of x, parameters [0], [1], ...",
                             &component.formula);
    ImGui::SameLine();
    if (ImGui::Button("Apply"))
    {
        FormulaCheckResult check = validator ? validator(component.formula)
                                             : FormulaCheckResult{ false, 0, "no validator available" };
        m_formulaMessageOwner = &component;
        m_formulaMessageIsError = !check.valid;
        if (check.valid)
        {
            std::vector<FitParameter> parameters;
            for (int i = 0; i < check.parameterCount; ++i)
            {
                double value = i < static_cast<int>(component.parameters.size())
                                   ? component.parameters[i].value
                                   : 1.0;
                parameters.push_back({ "p" + std::to_string(i), value,
                                       false, std::nullopt, std::nullopt });
            }
            component.parameters = std::move(parameters);
            m_formulaMessage = check.parameterCount == 0
                                   ? "valid, no parameters"
                                   : "valid, " + std::to_string(check.parameterCount) + " parameter(s)";
        }
        else
        {
            m_formulaMessage = check.message;
        }
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("The formula must not contain an overall scale\n"
                          "parameter; the amplitude provides it.");
    }

    if (m_formulaMessageOwner == &component && !m_formulaMessage.empty())
    {
        ImGui::TextColored(m_formulaMessageIsError ? theme.statusError : theme.statusGood,
                           "%s", m_formulaMessage.c_str());
    }

    ImGui::PopID();
}

void FitModelPanel::DrawStatisticSection(FitModel& model, const Theme& theme)
{
    if (!SectionHeader("Fit Settings", theme, ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    float comboWidth = ImGui::GetContentRegionAvail().x * 0.5f;

    const char* statisticNames[] = { "Chi-square", "Poisson likelihood" };
    int statistic = model.statistic == FitStatistic::ChiSquare ? 0 : 1;
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::Combo("statistic", &statistic, statisticNames, 2))
    {
        model.statistic = statistic == 0 ? FitStatistic::ChiSquare : FitStatistic::PoissonLikelihood;
    }
    HelpMarker("Chi-square matches ROOT's convention. Poisson likelihood is the\n"
               "better choice for low-count spectra.");

    // Minuit2 is the only minimizer, by design -- so we say so plainly.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Minimizer: Minuit2");
    HelpMarker("GIGGLE fits with Minuit2 - ROOT's modern, actively maintained\n"
               "minimizer, which supersedes the older Minuit and Fumili. Other\n"
               "ROOT minimizers (GSL, Genetic) need a specially built ROOT, so\n"
               "they aren't offered.");

    const char* algorithmNames[] = { "MIGRAD", "SIMPLEX", "SCAN", "Combination" };
    int algorithm = static_cast<int>(model.algorithm);
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::Combo("algorithm", &algorithm, algorithmNames, 4))
    {
        model.algorithm = static_cast<MinimizerAlgorithm>(algorithm);
    }
    HelpMarker("MIGRAD is the standard. SIMPLEX and SCAN are slower fallbacks\n"
               "for fits that won't converge; Combination runs MIGRAD then\n"
               "SIMPLEX. Uncertainties are filled in afterward either way.");

    const char* uncertaintyNames[] = { "Parabolic", "MINOS" };
    int uncertainties = static_cast<int>(model.uncertainties);
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::Combo("uncertainties", &uncertainties, uncertaintyNames, 2))
    {
        model.uncertainties = static_cast<FitUncertainties>(uncertainties);
    }
    HelpMarker("Parabolic (HESSE) gives fast, symmetric errors. MINOS profiles\n"
               "each parameter for asymmetric errors - slower, but better near\n"
               "bounds or for non-parabolic likelihoods. The counts keep the\n"
               "symmetric error for now.");

    ImGui::Checkbox("Integrate over bins", &model.integrateBins);
    HelpMarker("Compare each bin to the function's integral over the bin\n"
               "instead of its value at the bin center. More accurate for\n"
               "narrow or steeply curving peaks; slightly slower. Negligible\n"
               "when peaks span many bins.");

    if (SubTree("Advanced", theme))
    {
        ImGui::Checkbox("Ignore bin errors (unweighted)", &model.ignoreBinErrors);
        HelpMarker("Treat every bin as equally weighted (error = 1) instead of\n"
                   "sqrt(N). For unweighted data.");

        // Empty bins are always included for a likelihood fit.
        bool likelihood = model.statistic == FitStatistic::PoissonLikelihood;
        ImGui::BeginDisabled(likelihood);
        bool countEmpty = model.countEmptyBins || likelihood;
        if (ImGui::Checkbox("Count empty bins", &countEmpty))
        {
            model.countEmptyBins = countEmpty;
        }
        ImGui::EndDisabled();
        HelpMarker("Include zero-count bins in the fit. Always on for Poisson\n"
                   "likelihood.");

        float fieldWidth = ImGui::GetContentRegionAvail().x * 0.4f;

        ImGui::SetNextItemWidth(fieldWidth);
        ImGui::InputDouble("max tolerance", &model.tolerance, 0.0, 0.0, "%.4g");
        if (model.tolerance < 0.0)
        {
            model.tolerance = 0.0;
        }
        HelpMarker("Minuit2 convergence tolerance. 0 keeps Minuit2's default.");

        ImGui::SetNextItemWidth(fieldWidth);
        ImGui::InputInt("max iterations", &model.maxIterations, 0);
        if (model.maxIterations < 0)
        {
            model.maxIterations = 0;
        }
        HelpMarker("Cap on minimizer iterations. 0 keeps Minuit2's default.");

        const char* printNames[] = { "Quiet", "Normal", "Verbose" };
        int printLevel = static_cast<int>(model.printLevel);
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::Combo("print level", &printLevel, printNames, 3))
        {
            model.printLevel = static_cast<PrintLevel>(printLevel);
        }
        HelpMarker("Prints Minuit2's iteration detail to the terminal - visible\n"
                   "only when GIGGLE is launched from a console.");

        ImGui::TreePop();
    }
}

// The amplitude is a real fit parameter, displayed in plot units: the
// component's counts per bin at its reference point ("height" of a peak,
// "level" of a background). The conversion is the local bin width -- a
// constant -- so the fix checkbox and future bounds act on exactly what
// the user sees. Custom shapes have no reference point that equals 1, so
// their amplitude shows raw.
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
    double binWidth = component.shape == ShapeKind::Custom ? 1.0 : BinWidthAt(histogram, position);

    double display = component.amplitude.value * binWidth;

    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("In counts, as read off the plot.");
    }
    FixedLockGlyph(component.amplitude.fixed);

    bool bounded = component.amplitude.lowerBound.has_value()
                   || component.amplitude.upperBound.has_value();
    if (bounded)
    {
        ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        tint.z += 0.10f;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, tint);
    }

    DragLimits limits = LimitsFor(component.amplitude, binWidth);

    float checkboxWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - checkboxWidth - 40.0f);
    ImGui::PushFont(monoFont, 0.0f);
    if (ImGui::DragScalar("##value", ImGuiDataType_Double, &display,
                          DragSpeedFor(display), limits.minimumPointer, limits.maximumPointer,
                          "%.6g"))
    {
        component.amplitude.value = display / binWidth;
    }
    ImGui::PopFont();
    if (bounded)
    {
        ImGui::PopStyleColor();
    }
    DrawBoundsPopup(component.amplitude, binWidth);

    ImGui::SameLine();
    ImGui::Checkbox("fix", &component.amplitude.fixed);

    ImGui::PopID();
}

bool FitModelPanel::DrawParameterRow(FitParameter& parameter, const char* id, ImFont* monoFont)
{
    ImGui::PushID(id);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(id);
    FixedLockGlyph(parameter.fixed);

    // Right-align the fix checkbox; the value field takes the middle.
    float checkboxWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    float valueStart = ImGui::GetContentRegionAvail().x * 0.35f;

    bool bounded = parameter.lowerBound.has_value() || parameter.upperBound.has_value();
    if (bounded)
    {
        // A tinted field marks a bounded parameter.
        ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        tint.z += 0.10f;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, tint);
    }

    DragLimits limits = LimitsFor(parameter, 1.0);

    ImGui::SameLine(valueStart);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - checkboxWidth - 40.0f);
    ImGui::PushFont(monoFont, 0.0f);
    bool changed = ImGui::DragScalar("##value", ImGuiDataType_Double, &parameter.value,
                                     DragSpeedFor(parameter.value),
                                     limits.minimumPointer, limits.maximumPointer, "%.6g");
    ImGui::PopFont();
    if (bounded)
    {
        ImGui::PopStyleColor();
    }
    DrawBoundsPopup(parameter, 1.0);

    ImGui::SameLine();
    ImGui::Checkbox("fix", &parameter.fixed);

    ImGui::PopID();
    return changed;
}

} // namespace giggle
