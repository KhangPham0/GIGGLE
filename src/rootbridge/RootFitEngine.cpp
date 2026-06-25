#include "RootFitEngine.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "Fit/BinData.h"
#include "Fit/Fitter.h"
#include "HFitInterface.h"
#include "Math/MinimizerOptions.h"
#include "Math/WrappedMultiTF1.h"
#include "TF1.h"
#include "TF1NormSum.h"
#include "TH1D.h"
#include "TROOT.h"

#include "core/Shapes.h"
#include "FormulaSupport.h"

namespace giggle {

namespace {

// Evaluates one component's unit-amplitude shape from a raw parameter
// array, custom formulas included.
double SlotShapeValue(const FitComponent& component, const double* parameters, int count,
                      double x, double pivot)
{
    if (component.shape == ShapeKind::Custom)
    {
        return EvaluateFormulaRaw(component.formula, parameters, count, x);
    }
    return ShapeValue(component.shape, parameters, count, x, pivot);
}

// Where each component's parameters sit in the flat parameter array: for
// every component (peaks first, then background), the amplitude followed
// by the shape parameters. This is the order FitResult::covariance
// documents.
struct ComponentSlot
{
    const FitComponent* component;
    int amplitudeIndex;
    int firstShapeIndex;
    int shapeCount;
};

std::vector<ComponentSlot> LayOutParameters(const FitModel& model)
{
    std::vector<ComponentSlot> slots;
    int nextIndex = 0;

    auto add = [&](const FitComponent& component) {
        ComponentSlot slot;
        slot.component = &component;
        slot.amplitudeIndex = nextIndex++;
        slot.firstShapeIndex = nextIndex;
        slot.shapeCount = static_cast<int>(component.parameters.size());
        nextIndex += slot.shapeCount;
        slots.push_back(slot);
    };

    for (const FitComponent& peak : model.peaks)
    {
        add(peak);
    }
    for (const FitComponent& component : model.background)
    {
        add(component);
    }
    return slots;
}

TH1D MakeRootHistogram(const HistogramData& data)
{
    TH1::AddDirectory(false);
    TH1D histogram("giggle_fit_data", data.title.c_str(),
                   data.BinCount(), data.binEdges.data());

    for (int bin = 0; bin < data.BinCount(); ++bin)
    {
        histogram.SetBinContent(bin + 1, data.counts[bin]);
    }
    if (!data.errors.empty())
    {
        histogram.Sumw2();
        for (int bin = 0; bin < data.BinCount(); ++bin)
        {
            histogram.SetBinError(bin + 1, data.errors[bin]);
        }
    }
    return histogram;
}

// One-sided bounds need the ROOT::Fit parameter settings: faking them with
// huge two-sided TF1 limits distorts Minuit's parameter transformation and
// ruins both convergence and the errors.
void ConfigureParameter(ROOT::Fit::ParameterSettings& settings, const FitParameter& parameter)
{
    settings.SetValue(parameter.value);

    if (parameter.fixed)
    {
        settings.Fix();
        return;
    }
    if (parameter.lowerBound.has_value() && parameter.upperBound.has_value())
    {
        settings.SetLimits(parameter.lowerBound.value(), parameter.upperBound.value());
    }
    else if (parameter.lowerBound.has_value())
    {
        settings.SetLowerLimit(parameter.lowerBound.value());
    }
    else if (parameter.upperBound.has_value())
    {
        settings.SetUpperLimit(parameter.upperBound.value());
    }
}

bool HasUniformBins(const HistogramData& data)
{
    if (data.BinCount() < 1)
    {
        return false;
    }
    double first = data.binEdges[1] - data.binEdges[0];
    for (int bin = 1; bin < data.BinCount(); ++bin)
    {
        double width = data.binEdges[bin + 1] - data.binEdges[bin];
        if (std::abs(width - first) > 1e-9 * first)
        {
            return false;
        }
    }
    return true;
}

// The model's algorithm as Minuit2's algorithm string.
const char* RootAlgorithm(MinimizerAlgorithm algorithm)
{
    switch (algorithm)
    {
        case MinimizerAlgorithm::Migrad:      return "Migrad";
        case MinimizerAlgorithm::Simplex:     return "Simplex";
        case MinimizerAlgorithm::Scan:        return "Scan";
        case MinimizerAlgorithm::Combination: return "Combined";
    }
    return "Migrad";
}

// Minuit2 print levels: 0 silent, higher is chattier (to the terminal).
int RootPrintLevel(PrintLevel level)
{
    switch (level)
    {
        case PrintLevel::Quiet:   return 0;
        case PrintLevel::Normal:  return 1;
        case PrintLevel::Verbose: return 3;
    }
    return 0;
}

// Minimizer choices applied to both the main fit and the verification fit,
// so the two minimize the same way. The data-side options (integral,
// weights, empty bins) live on the shared BinData instead.
void ApplyMinimizerConfig(ROOT::Fit::Fitter& fitter, const FitModel& model)
{
    fitter.Config().SetMinimizer("Minuit2", RootAlgorithm(model.algorithm));
    ROOT::Math::MinimizerOptions& options = fitter.Config().MinimizerOptions();
    if (model.tolerance > 0.0)
    {
        options.SetTolerance(model.tolerance);
    }
    if (model.maxIterations > 0)
    {
        options.SetMaxIterations(static_cast<unsigned int>(model.maxIterations));
        options.SetMaxFunctionCalls(static_cast<unsigned int>(model.maxIterations));
    }
    options.SetPrintLevel(RootPrintLevel(model.printLevel));
}

// The data options shared by both fits: how each bin is compared to the
// model. Set once on the BinData the verification fit also uses.
ROOT::Fit::DataOptions MakeDataOptions(const FitModel& model, bool poisson)
{
    ROOT::Fit::DataOptions options;
    options.fIntegral = model.integrateBins;        // integral over the bin vs center value
    options.fErrors1 = model.ignoreBinErrors;       // every bin weight 1
    options.fUseEmpty = poisson || model.countEmptyBins; // likelihood always needs empties
    return options;
}

// Re-fits the same data through ROOT's TF1NormSum, whose coefficients are
// the components' in-range integrals, and compares against our counts.
// An independent path to the same numbers: if the two disagree, something
// is wrong and the user must know. With MINOS it doubles as the source of
// the asymmetric count errors -- the coefficients ARE the counts, so their
// profiled errors are written back onto the result's per-component counts.
CrossCheck RunNormSumCrossCheck(const HistogramData& data, const TH1D& rootData,
                                const ROOT::Fit::BinData& binData, const FitModel& model,
                                const std::vector<ComponentSlot>& slots, const FitRange& range,
                                bool poisson, FitResult& result)
{
    CrossCheck check;

    if (!HasUniformBins(data))
    {
        check.detail = "this histogram has variable bin widths";
        return check;
    }
    for (const ComponentSlot& slot : slots)
    {
        if (slot.component->amplitude.fixed)
        {
            check.detail = "a fixed height cannot be mirrored in the verification fit";
            return check;
        }
    }

    (void)rootData;
    double binWidth = data.binEdges[1] - data.binEdges[0];
    double pivot = RangePivot(range);
    int componentCount = static_cast<int>(slots.size());

    // Our fitted counts and parameters per component, in model order. The
    // pointers are non-const so MINOS count errors can be written back.
    std::vector<ValueWithError> ourCounts;
    std::vector<ComponentResult*> ourComponents;
    for (ComponentResult& peak : result.peaks)
    {
        ourCounts.push_back(peak.counts);
        ourComponents.push_back(&peak);
    }
    for (ComponentResult& background : result.background)
    {
        ourCounts.push_back(background.counts);
        ourComponents.push_back(&background);
    }

    // One TF1 per component, built on the same core shape math. TF1NormSum
    // normalizes each over the range, so no amplitude parameter appears,
    // and its coefficients are in y*x units: counts * bin width.
    std::vector<std::unique_ptr<TF1>> shapes;
    std::vector<TF1*> shapePointers;
    std::vector<double> coefficients;

    for (size_t i = 0; i < slots.size(); ++i)
    {
        const ComponentSlot& slot = slots[i];
        const FitComponent* component = slot.component;
        int shapeCount = slot.shapeCount;
        auto shapeOnly = [component, shapeCount, pivot](double* xs, double* p) {
            return SlotShapeValue(*component, p, shapeCount, xs[0], pivot);
        };
        auto function = std::make_unique<TF1>(
            ("normsum_" + slot.component->label).c_str(), shapeOnly, range.min, range.max, shapeCount);
        for (int j = 0; j < shapeCount; ++j)
        {
            function->SetParameter(j, ourComponents[i]->parameters[j].value);
        }
        shapePointers.push_back(function.get());
        shapes.push_back(std::move(function));

        coefficients.push_back(ourCounts[i].value * binWidth);
    }

    TF1NormSum normSum(shapePointers, coefficients);
    TF1 combined("normsum_combined", normSum, range.min, range.max, normSum.GetNpar());
    std::vector<double> initial = normSum.GetParameters();
    combined.SetParameters(initial.data());

    // Parameter order: all coefficients first, then each component's shape
    // parameters in sequence.
    ROOT::Math::WrappedMultiTF1 wrapped(combined, 1);
    ROOT::Fit::Fitter fitter;
    ApplyMinimizerConfig(fitter, model);
    // The coefficients are the counts, so MINOS here profiles the counts.
    if (model.uncertainties == FitUncertainties::Minos)
    {
        fitter.Config().SetMinosErrors(true);
    }
    fitter.SetFunction(wrapped, false);

    // Mirror the model's constraints on the shape parameters, so a fit
    // whose result legitimately sits on a bound is compared against a
    // re-fit under the same rules rather than a freer one. Values start
    // from our fitted solution.
    int shapeIndex = componentCount;
    for (size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex)
    {
        const ComponentSlot& slot = slots[slotIndex];
        for (int i = 0; i < slot.shapeCount; ++i)
        {
            ROOT::Fit::ParameterSettings& settings = fitter.Config().ParSettings(shapeIndex);
            ConfigureParameter(settings, slot.component->parameters[i]);
            settings.SetValue(ourComponents[slotIndex]->parameters[i].value);
            ++shapeIndex;
        }
    }

    bool succeeded = poisson ? fitter.LikelihoodFit(binData, true) : fitter.Fit(binData);
    if (succeeded && fitter.Result().CovMatrixStatus() < 2)
    {
        fitter.CalculateHessErrors();
    }
    const ROOT::Fit::FitResult& fit = fitter.Result();
    if (!succeeded || !fit.IsValid())
    {
        check.performed = true;
        check.agreed = false;
        check.detail = "the verification fit did not converge";
        return check;
    }

    check.performed = true;
    check.agreed = true;
    double worstDeviation = 0.0;
    for (int i = 0; i < componentCount; ++i)
    {
        double referenceValue = fit.Parameter(i) / binWidth;
        double referenceError = fit.ParError(i) / binWidth;

        double scale = std::max(std::abs(ourCounts[i].value), std::abs(referenceValue));
        double errorScale = std::max(ourCounts[i].error, referenceError);
        double valueTolerance = std::max(0.01 * scale, 0.3 * errorScale);

        double valueDeviation = std::abs(ourCounts[i].value - referenceValue);
        double errorDeviation = std::abs(ourCounts[i].error - referenceError);

        if (valueDeviation > valueTolerance || errorDeviation > 0.10 * errorScale)
        {
            check.agreed = false;
        }
        if (scale > 0.0)
        {
            worstDeviation = std::max(worstDeviation, valueDeviation / scale);
        }
    }

    // When MINOS ran and the two fits agree, the coefficients' profiled
    // errors are the asymmetric count errors -- write them onto the counts.
    // The value stays from our fit (the two agree), so value and error are
    // consistent. The total is a derived sum, so it stays symmetric.
    if (model.uncertainties == FitUncertainties::Minos && check.agreed)
    {
        for (int i = 0; i < componentCount; ++i)
        {
            if (fit.HasMinosError(i))
            {
                ourComponents[i]->counts.errorLow = std::abs(fit.LowerError(i)) / binWidth;
                ourComponents[i]->counts.errorHigh = std::abs(fit.UpperError(i)) / binWidth;
            }
        }
    }

    char summary[128];
    std::snprintf(summary, sizeof(summary), "largest count deviation %.2g%%", worstDeviation * 100.0);
    check.detail = summary;
    return check;
}

// True when a free parameter has converged onto one of its bounds, where
// the parabolic uncertainty is unreliable.
bool SitsOnBound(const FitParameter& parameter, double fittedValue, double fittedError)
{
    if (parameter.fixed)
    {
        return false;
    }
    double tolerance = std::max(0.01 * fittedError, 1e-9 * std::max(1.0, std::abs(fittedValue)));
    if (parameter.lowerBound.has_value() && std::abs(fittedValue - parameter.lowerBound.value()) <= tolerance)
    {
        return true;
    }
    if (parameter.upperBound.has_value() && std::abs(fittedValue - parameter.upperBound.value()) <= tolerance)
    {
        return true;
    }
    return false;
}

// Cautions the user should see next to the numbers.
std::vector<std::string> CollectWarnings(const FitModel& fitted, const FitResult& result,
                                         const FitRange& range)
{
    std::vector<std::string> warnings;

    // Peaks whose core extends past the fit range: the reported counts are
    // only the in-range part.
    for (size_t i = 0; i < fitted.peaks.size() && i < result.peaks.size(); ++i)
    {
        const FitComponent& peak = fitted.peaks[i];
        if (peak.parameters.size() < 2)
        {
            continue;
        }
        double center = peak.parameters[0].value;
        double width = 3.0 * std::abs(peak.parameters[1].value); // a 3-sigma-like half width
        double lowReach = width;
        switch (peak.shape)
        {
            case ShapeKind::Gaussian:
            case ShapeKind::Lorentzian:
            case ShapeKind::Voigt:
                break;
            case ShapeKind::GaussianTail:
                // The low-energy tail reaches further down.
                if (peak.parameters.size() >= 4)
                {
                    lowReach = width + 3.0 * std::abs(peak.parameters[3].value);
                }
                break;
            default:
                continue;
        }

        if (center - lowReach < range.min || center + width > range.max)
        {
            warnings.push_back(peak.label + " extends past the fit range; its counts cover only the in-range part");
        }
    }

    // Parameters sitting on a bound.
    auto checkComponent = [&warnings](const FitComponent& component, const ComponentResult& fit) {
        if (SitsOnBound(component.amplitude, fit.amplitude.value, fit.amplitude.error))
        {
            warnings.push_back(component.label + " amplitude is at its bound; the uncertainties are unreliable there");
        }
        for (size_t j = 0; j < component.parameters.size() && j < fit.parameters.size(); ++j)
        {
            if (SitsOnBound(component.parameters[j], fit.parameters[j].value, fit.parameters[j].error))
            {
                warnings.push_back(component.label + " " + component.parameters[j].name
                                   + " is at its bound; the uncertainties are unreliable there");
            }
        }
    };
    for (size_t i = 0; i < fitted.peaks.size() && i < result.peaks.size(); ++i)
    {
        checkComponent(fitted.peaks[i], result.peaks[i]);
    }
    for (size_t i = 0; i < fitted.background.size() && i < result.background.size(); ++i)
    {
        checkComponent(fitted.background[i], result.background[i]);
    }

    return warnings;
}

// One fitted parameter, with the symmetric (HESSE) error always and the
// asymmetric MINOS magnitudes when they were computed and exist for it.
ValueWithError MakeValue(const ROOT::Fit::FitResult& fit, int index, bool minos)
{
    ValueWithError value;
    value.value = fit.Parameter(index);
    value.error = fit.ParError(index);
    if (minos && fit.HasMinosError(index))
    {
        value.errorLow = std::abs(fit.LowerError(index));
        value.errorHigh = std::abs(fit.UpperError(index));
    }
    return value;
}

ComponentResult ReadComponentResult(const ROOT::Fit::FitResult& fit, const ComponentSlot& slot,
                                    bool minos)
{
    ComponentResult result;
    result.label = slot.component->label;
    result.amplitude = MakeValue(fit, slot.amplitudeIndex, minos);
    for (int i = 0; i < slot.shapeCount; ++i)
    {
        result.parameters.push_back(MakeValue(fit, slot.firstShapeIndex + i, minos));
    }
    return result;
}

} // namespace

RootFitEngine::RootFitEngine()
{
    ROOT::EnableThreadSafety();
    ROOT::Math::MinimizerOptions::SetDefaultMinimizer("Minuit2");
}

FitResult RootFitEngine::Fit(const HistogramData& histogram, const FitModel& model)
{
    FitResult result;

    if (model.peaks.empty() && model.background.empty())
    {
        result.message = "the model has no components";
        return result;
    }
    if (model.range.max <= model.range.min)
    {
        result.message = "the fit range is empty";
        return result;
    }
    // Every shape must be evaluable; custom formulas are validated here so
    // a bad one fails with a clear message instead of a broken fit.
    auto checkComponent = [&result](const FitComponent& component) {
        if (component.shape == ShapeKind::Custom)
        {
            FormulaCheckResult check = ValidateFormula(component.formula);
            if (!check.valid)
            {
                result.message = component.label + ": " + check.message;
                return false;
            }
            if (check.parameterCount != static_cast<int>(component.parameters.size()))
            {
                result.message = component.label + ": the formula uses "
                                 + std::to_string(check.parameterCount) + " parameters but "
                                 + std::to_string(component.parameters.size()) + " are defined";
                return false;
            }
            return true;
        }
        if (!ShapeIsImplemented(component.shape))
        {
            result.message = std::string("shape \"") + ShapeKindName(component.shape)
                             + "\" is not supported yet";
            return false;
        }
        return true;
    };
    for (const FitComponent& peak : model.peaks)
    {
        if (!checkComponent(peak))
        {
            return result;
        }
    }
    for (const FitComponent& component : model.background)
    {
        if (!checkComponent(component))
        {
            return result;
        }
    }

    try
    {
        std::vector<ComponentSlot> slots = LayOutParameters(model);
        int parameterCount = 0;
        for (const ComponentSlot& slot : slots)
        {
            parameterCount += 1 + slot.shapeCount;
        }

        TH1D data = MakeRootHistogram(histogram);

        // The fit selects whole bins, so everything -- data window, shape
        // pivot, count extraction -- uses the range snapped to bin edges.
        FitRange range = SnapRangeToBinEdges(histogram, model.range);
        double pivot = RangePivot(range);

        // The model value is a density (amplitude * unit-amplitude shape);
        // a bin's expected content is density * bin width.
        auto evaluate = [slots, pivot, &histogram](double* xs, double* p) {
            double x = xs[0];
            double density = 0.0;
            for (const ComponentSlot& slot : slots)
            {
                density += p[slot.amplitudeIndex]
                           * SlotShapeValue(*slot.component, p + slot.firstShapeIndex,
                                            slot.shapeCount, x, pivot);
            }
            return density * BinWidthAt(histogram, x);
        };

        TF1 function("giggle_fit", evaluate, range.min, range.max, parameterCount);
        for (const ComponentSlot& slot : slots)
        {
            const FitComponent& component = *slot.component;
            function.SetParName(slot.amplitudeIndex, (component.label + " amplitude").c_str());
            function.SetParameter(slot.amplitudeIndex, component.amplitude.value);
            for (int i = 0; i < slot.shapeCount; ++i)
            {
                function.SetParName(slot.firstShapeIndex + i,
                                    (component.label + " " + component.parameters[i].name).c_str());
                function.SetParameter(slot.firstShapeIndex + i, component.parameters[i].value);
            }
        }

        bool poisson = model.statistic == FitStatistic::PoissonLikelihood;

        // The data points inside the fit range. The Poisson likelihood must
        // see empty bins too; the chi-square skips them (no defined error).
        ROOT::Fit::DataOptions dataOptions = MakeDataOptions(model, poisson);
        ROOT::Fit::DataRange dataRange(range.min, range.max);
        ROOT::Fit::BinData binData(dataOptions, dataRange);
        ROOT::Fit::FillData(binData, &data);

        ROOT::Math::WrappedMultiTF1 wrappedFunction(function, 1);
        ROOT::Fit::Fitter fitter;
        ApplyMinimizerConfig(fitter, model);
        // MINOS runs as part of the fit, giving asymmetric parameter errors.
        if (model.uncertainties == FitUncertainties::Minos)
        {
            fitter.Config().SetMinosErrors(true);
        }
        fitter.SetFunction(wrappedFunction, false);

        for (const ComponentSlot& slot : slots)
        {
            const FitComponent& component = *slot.component;
            ConfigureParameter(fitter.Config().ParSettings(slot.amplitudeIndex), component.amplitude);
            for (int i = 0; i < slot.shapeCount; ++i)
            {
                ConfigureParameter(fitter.Config().ParSettings(slot.firstShapeIndex + i),
                                   component.parameters[i]);
            }
        }

        bool succeeded = poisson ? fitter.LikelihoodFit(binData, true) : fitter.Fit(binData);
        // SIMPLEX and SCAN locate the minimum but leave no covariance; HESSE
        // fills it in so every algorithm still yields uncertainties.
        if (succeeded && fitter.Result().CovMatrixStatus() < 2)
        {
            fitter.CalculateHessErrors();
        }
        const ROOT::Fit::FitResult& fit = fitter.Result();

        result.converged = succeeded && fit.IsValid();
        result.message = result.converged
                             ? "fit converged"
                             : "fit did not converge (minimizer status "
                                   + std::to_string(fit.Status()) + ")";

        result.chiSquare = fit.Chi2();
        result.degreesOfFreedom = static_cast<int>(fit.Ndf());

        result.covariance.assign(parameterCount, std::vector<double>(parameterCount, 0.0));
        for (int row = 0; row < parameterCount; ++row)
        {
            for (int column = 0; column < parameterCount; ++column)
            {
                result.covariance[row][column] = fit.CovMatrix(row, column);
            }
        }

        // The fitted model: the source for counts, curves, and totals.
        FitModel fitted = model;
        fitted.range = range;

        bool minos = model.uncertainties == FitUncertainties::Minos;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            ComponentResult componentResult = ReadComponentResult(fit, slots[i], minos);
            if (i < model.peaks.size())
            {
                result.peaks.push_back(componentResult);
            }
            else
            {
                result.background.push_back(componentResult);
            }
        }
        ApplyFitResult(result, fitted);

        // Counts in range per component: N = A * integral(shape), with the
        // error from the component's covariance block.
        std::vector<FitComponent*> fittedComponents;
        for (FitComponent& peak : fitted.peaks)
        {
            fittedComponents.push_back(&peak);
        }
        for (FitComponent& component : fitted.background)
        {
            fittedComponents.push_back(&component);
        }

        // Assembled across all parameters for the total's variance.
        std::vector<double> totalGradient(parameterCount, 0.0);

        for (size_t i = 0; i < slots.size(); ++i)
        {
            const ComponentSlot& slot = slots[i];
            const FitComponent& fittedComponent = *fittedComponents[i];

            int blockSize = 1 + slot.shapeCount;
            std::vector<std::vector<double>> block(blockSize, std::vector<double>(blockSize, 0.0));
            for (int row = 0; row < blockSize; ++row)
            {
                for (int column = 0; column < blockSize; ++column)
                {
                    block[row][column] = fit.CovMatrix(slot.amplitudeIndex + row,
                                                       slot.amplitudeIndex + column);
                }
            }

            ValueWithError counts = ComponentCountsWithError(fittedComponent, range, block);
            ComponentResult& target =
                i < model.peaks.size() ? result.peaks[i] : result.background[i - model.peaks.size()];
            target.counts = counts;
            result.totalCounts.value += counts.value;

            std::vector<double> gradient = ComponentCountsGradient(fittedComponent, range);
            for (int j = 0; j < blockSize; ++j)
            {
                totalGradient[slot.amplitudeIndex + j] = gradient[j];
            }
        }

        double totalVariance = 0.0;
        for (int row = 0; row < parameterCount; ++row)
        {
            for (int column = 0; column < parameterCount; ++column)
            {
                totalVariance += totalGradient[row] * fit.CovMatrix(row, column) * totalGradient[column];
            }
        }
        result.totalCounts.error = std::sqrt(totalVariance);

        // Curves of the fitted model, in the same units as the data.
        result.curves = SampleModelCurves(fitted, 400, &histogram);

        result.warnings = CollectWarnings(fitted, result, range);

        // Every converged fit is checked against an independent ROOT
        // implementation of the same counts.
        if (result.converged)
        {
            result.normSumCheck = RunNormSumCrossCheck(histogram, data, binData, model, slots,
                                                       range, poisson, result);
        }
    }
    catch (const std::exception& error)
    {
        result.converged = false;
        result.message = error.what();
    }

    return result;
}

} // namespace giggle
