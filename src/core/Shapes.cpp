#include "Shapes.h"

#include <algorithm>
#include <cmath>

namespace giggle {

namespace {

const double kSqrt2 = std::sqrt(2.0);
const double kSqrtHalfPi = std::sqrt(M_PI / 2.0);

// The i-th parameter, or fallback when fewer are given.
double Param(const double* parameters, int count, int index, double fallback = 0.0)
{
    return index < count ? parameters[index] : fallback;
}

// Copies a component's parameter values into a flat array. Implemented
// shapes have at most 3 parameters.
int FlattenParameters(const FitComponent& component, double* buffer, int capacity)
{
    int count = std::min(static_cast<int>(component.parameters.size()), capacity);
    for (int i = 0; i < count; ++i)
    {
        buffer[i] = component.parameters[i].value;
    }
    return count;
}

} // namespace

bool ShapeIsImplemented(ShapeKind kind)
{
    return kind != ShapeKind::Voigt && kind != ShapeKind::Custom;
}

double ShapeValue(ShapeKind kind, const double* p, int count, double x, double pivot)
{
    switch (kind)
    {
        case ShapeKind::Gaussian:
        {
            double mean = Param(p, count, 0);
            double sigma = Param(p, count, 1, 1.0);
            double z = (x - mean) / sigma;
            return std::exp(-0.5 * z * z);
        }
        case ShapeKind::Lorentzian:
        {
            double mean = Param(p, count, 0);
            double gamma = Param(p, count, 1, 1.0);
            double z = (x - mean) / gamma;
            return 1.0 / (1.0 + z * z);
        }
        case ShapeKind::Constant:
            return 1.0;
        case ShapeKind::Linear:
            return 1.0 + Param(p, count, 0) * (x - pivot);
        case ShapeKind::Quadratic:
        {
            double u = x - pivot;
            return 1.0 + Param(p, count, 0) * u + Param(p, count, 1) * u * u;
        }
        case ShapeKind::Exponential:
            return std::exp(Param(p, count, 0) * (x - pivot));
        case ShapeKind::Voigt:
        case ShapeKind::Custom:
            return 0.0; // not implemented yet
    }
    return 0.0;
}

double ShapeIntegral(ShapeKind kind, const double* p, int count, double a, double b, double pivot)
{
    switch (kind)
    {
        case ShapeKind::Gaussian:
        {
            double mean = Param(p, count, 0);
            double sigma = Param(p, count, 1, 1.0);
            double za = (a - mean) / (sigma * kSqrt2);
            double zb = (b - mean) / (sigma * kSqrt2);
            return sigma * kSqrtHalfPi * (std::erf(zb) - std::erf(za));
        }
        case ShapeKind::Lorentzian:
        {
            double mean = Param(p, count, 0);
            double gamma = Param(p, count, 1, 1.0);
            return gamma * (std::atan((b - mean) / gamma) - std::atan((a - mean) / gamma));
        }
        case ShapeKind::Constant:
            return b - a;
        case ShapeKind::Linear:
        {
            double slope = Param(p, count, 0);
            double ua = a - pivot;
            double ub = b - pivot;
            return (b - a) + slope / 2.0 * (ub * ub - ua * ua);
        }
        case ShapeKind::Quadratic:
        {
            double slope = Param(p, count, 0);
            double curvature = Param(p, count, 1);
            double ua = a - pivot;
            double ub = b - pivot;
            return (b - a) + slope / 2.0 * (ub * ub - ua * ua)
                   + curvature / 3.0 * (ub * ub * ub - ua * ua * ua);
        }
        case ShapeKind::Exponential:
        {
            double slope = Param(p, count, 0);
            if (std::abs(slope) < 1e-12)
            {
                return b - a; // exp(slope u) ~ 1
            }
            return (std::exp(slope * (b - pivot)) - std::exp(slope * (a - pivot))) / slope;
        }
        case ShapeKind::Voigt:
        case ShapeKind::Custom:
            return 0.0; // not implemented yet
    }
    return 0.0;
}

double ShapeValue(const FitComponent& component, const FitRange& range, double x)
{
    double parameters[8];
    int count = FlattenParameters(component, parameters, 8);
    return ShapeValue(component.shape, parameters, count, x, RangePivot(range));
}

double ShapeIntegral(const FitComponent& component, const FitRange& range, double a, double b)
{
    double parameters[8];
    int count = FlattenParameters(component, parameters, 8);
    return ShapeIntegral(component.shape, parameters, count, a, b, RangePivot(range));
}

double ComponentDensity(const FitComponent& component, const FitRange& range, double x)
{
    return component.amplitude.value * ShapeValue(component, range, x);
}

double ComponentCounts(const FitComponent& component, const FitRange& range)
{
    return component.amplitude.value * ShapeIntegral(component, range, range.min, range.max);
}

std::vector<double> ComponentCountsGradient(const FitComponent& component, const FitRange& range)
{
    std::vector<double> gradient;
    gradient.reserve(1 + component.parameters.size());

    // dN/dA: the shape integral itself.
    gradient.push_back(ShapeIntegral(component, range, range.min, range.max));

    // dN/dtheta_j: central finite differences on the integral.
    FitComponent probe = component;
    for (size_t j = 0; j < component.parameters.size(); ++j)
    {
        double value = component.parameters[j].value;
        double step = std::max(std::abs(value) * 1e-6, 1e-9);

        probe.parameters[j].value = value + step;
        double above = ShapeIntegral(probe, range, range.min, range.max);
        probe.parameters[j].value = value - step;
        double below = ShapeIntegral(probe, range, range.min, range.max);
        probe.parameters[j].value = value;

        gradient.push_back(component.amplitude.value * (above - below) / (2.0 * step));
    }
    return gradient;
}

ValueWithError ComponentCountsWithError(const FitComponent& component, const FitRange& range,
                                        const std::vector<std::vector<double>>& covarianceBlock)
{
    ValueWithError counts;
    counts.value = ComponentCounts(component, range);

    std::vector<double> gradient = ComponentCountsGradient(component, range);
    double variance = 0.0;
    for (size_t i = 0; i < gradient.size() && i < covarianceBlock.size(); ++i)
    {
        for (size_t j = 0; j < gradient.size() && j < covarianceBlock[i].size(); ++j)
        {
            variance += gradient[i] * covarianceBlock[i][j] * gradient[j];
        }
    }
    counts.error = variance > 0.0 ? std::sqrt(variance) : 0.0;
    return counts;
}

std::optional<ValueWithError> PeakCentroid(ShapeKind kind, const ComponentResult& result)
{
    switch (kind)
    {
        case ShapeKind::Gaussian:
        case ShapeKind::Lorentzian:
            // The first parameter is the mean.
            if (!result.parameters.empty())
            {
                return result.parameters[0];
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

std::optional<ValueWithError> PeakFWHM(ShapeKind kind, const ComponentResult& result)
{
    switch (kind)
    {
        case ShapeKind::Gaussian:
        {
            // FWHM = 2 sqrt(2 ln 2) sigma
            const double factor = 2.0 * std::sqrt(2.0 * std::log(2.0));
            if (result.parameters.size() >= 2)
            {
                return ValueWithError{ factor * result.parameters[1].value,
                                       factor * result.parameters[1].error };
            }
            return std::nullopt;
        }
        case ShapeKind::Lorentzian:
        {
            // gamma is the half width at half maximum.
            if (result.parameters.size() >= 2)
            {
                return ValueWithError{ 2.0 * result.parameters[1].value,
                                       2.0 * result.parameters[1].error };
            }
            return std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

double BinWidthAt(const HistogramData& histogram, double x)
{
    const std::vector<double>& edges = histogram.binEdges;
    if (edges.size() < 2)
    {
        return 1.0;
    }

    // The first edge above x; clamped so x outside the histogram gets the
    // width of the nearest bin.
    auto upper = std::upper_bound(edges.begin(), edges.end(), x);
    if (upper == edges.begin())
    {
        ++upper;
    }
    if (upper == edges.end())
    {
        --upper;
    }
    return *upper - *(upper - 1);
}

FitRange SnapRangeToBinEdges(const HistogramData& histogram, FitRange range)
{
    const std::vector<double>& edges = histogram.binEdges;
    if (edges.size() < 2)
    {
        return range;
    }

    auto nearestEdge = [&edges](double x) {
        auto above = std::lower_bound(edges.begin(), edges.end(), x);
        if (above == edges.begin())
        {
            return edges.front();
        }
        if (above == edges.end())
        {
            return edges.back();
        }
        double below = *(above - 1);
        return (x - below) < (*above - x) ? below : *above;
    };

    FitRange snapped;
    snapped.min = nearestEdge(range.min);
    snapped.max = nearestEdge(range.max);

    // Never collapse to zero bins.
    if (snapped.max <= snapped.min)
    {
        auto edge = std::lower_bound(edges.begin(), edges.end(), snapped.min);
        snapped.max = (edge + 1) != edges.end() ? *(edge + 1) : edges.back();
        if (snapped.max <= snapped.min)
        {
            snapped.min = *(edge - 1);
        }
    }
    return snapped;
}

ValueWithError CountsInRange(const HistogramData& histogram, const FitRange& range)
{
    ValueWithError total;
    double variance = 0.0;
    for (int bin = 0; bin < histogram.BinCount(); ++bin)
    {
        double center = 0.5 * (histogram.binEdges[bin] + histogram.binEdges[bin + 1]);
        if (center >= range.min && center <= range.max)
        {
            total.value += histogram.counts[bin];
            double error = histogram.BinError(bin);
            variance += error * error;
        }
    }
    total.error = std::sqrt(variance);
    return total;
}

FitComponent SuggestGaussianPeak(const HistogramData& histogram, const FitRange& range, double x)
{
    const std::vector<double>& edges = histogram.binEdges;
    int binCount = histogram.BinCount();

    // The bins inside the fit range, and the clicked bin clamped into them.
    auto binIndexOf = [&](double value) {
        auto above = std::upper_bound(edges.begin(), edges.end(), value);
        int bin = static_cast<int>(above - edges.begin()) - 1;
        return std::min(std::max(bin, 0), binCount - 1);
    };
    int firstBin = binIndexOf(range.min);
    int lastBin = binIndexOf(range.max);
    int clickedBin = std::min(std::max(binIndexOf(x), firstBin), lastBin);

    // The peak: the tallest bin within a few bins of the click.
    int peakBin = clickedBin;
    for (int bin = std::max(firstBin, clickedBin - 3);
         bin <= std::min(lastBin, clickedBin + 3); ++bin)
    {
        if (histogram.counts[bin] > histogram.counts[peakBin])
        {
            peakBin = bin;
        }
    }
    double mean = 0.5 * (edges[peakBin] + edges[peakBin + 1]);
    double peakCounts = histogram.counts[peakBin];

    // The local baseline: the smallest bin within a window around the peak.
    int window = std::max(15, (lastBin - firstBin) / 8);
    double baseline = peakCounts;
    for (int bin = std::max(firstBin, peakBin - window);
         bin <= std::min(lastBin, peakBin + window); ++bin)
    {
        baseline = std::min(baseline, histogram.counts[bin]);
    }

    // Sigma from a half-maximum scan away from the peak.
    double halfMax = baseline + 0.5 * (peakCounts - baseline);
    int left = peakBin;
    while (left > firstBin && histogram.counts[left] > halfMax)
    {
        --left;
    }
    int right = peakBin;
    while (right < lastBin && histogram.counts[right] > halfMax)
    {
        ++right;
    }
    double fwhm = 0.5 * (edges[right] + edges[right + 1]) - 0.5 * (edges[left] + edges[left + 1]);
    double sigma = fwhm / 2.354820045;

    // Keep the guess physical: at least a bin wide, at most a quarter of
    // the range.
    double binWidth = BinWidthAt(histogram, mean);
    sigma = std::max(sigma, binWidth);
    sigma = std::min(sigma, (range.max - range.min) / 4.0);

    FitComponent peak;
    peak.shape = ShapeKind::Gaussian;
    peak.amplitude = { "amplitude", std::max(peakCounts - baseline, 1.0) / binWidth,
                       false, 0.0, std::nullopt };
    peak.parameters = {
        { "mean", mean, false, std::nullopt, std::nullopt },
        { "sigma", sigma, false, std::nullopt, std::nullopt },
    };
    return peak;
}

FitCurves SampleModelCurves(const FitModel& model, int pointCount,
                            const HistogramData* histogramForUnits)
{
    FitCurves curves;
    if (pointCount < 2 || model.range.max <= model.range.min)
    {
        return curves;
    }

    curves.x.resize(pointCount);
    double step = (model.range.max - model.range.min) / (pointCount - 1);
    for (int i = 0; i < pointCount; ++i)
    {
        curves.x[i] = model.range.min + i * step;
    }

    // Densities become expected bin contents when scaled by the bin width.
    std::vector<double> unitScale(pointCount, 1.0);
    if (histogramForUnits != nullptr)
    {
        for (int i = 0; i < pointCount; ++i)
        {
            unitScale[i] = BinWidthAt(*histogramForUnits, curves.x[i]);
        }
    }

    curves.total.assign(pointCount, 0.0);

    auto sampleComponent = [&](const FitComponent& component) {
        std::vector<double> values(pointCount);
        for (int i = 0; i < pointCount; ++i)
        {
            values[i] = ComponentDensity(component, model.range, curves.x[i]) * unitScale[i];
            curves.total[i] += values[i];
        }
        curves.components.push_back(std::move(values));
    };

    for (const FitComponent& peak : model.peaks)
    {
        sampleComponent(peak);
    }
    for (const FitComponent& component : model.background)
    {
        sampleComponent(component);
    }

    return curves;
}

} // namespace giggle
