#include "Shapes.h"

#include <cmath>

namespace giggle {

namespace {

const double kSqrt2 = std::sqrt(2.0);
const double kSqrtHalfPi = std::sqrt(M_PI / 2.0);

// The i-th shape parameter, or fallback when the component has fewer.
double Param(const FitComponent& component, size_t index, double fallback = 0.0)
{
    return index < component.parameters.size() ? component.parameters[index].value : fallback;
}

} // namespace

double ShapeValue(const FitComponent& component, double x)
{
    switch (component.shape)
    {
        case ShapeKind::Gaussian:
        {
            double mean = Param(component, 0);
            double sigma = Param(component, 1, 1.0);
            double z = (x - mean) / sigma;
            return std::exp(-0.5 * z * z);
        }
        case ShapeKind::Lorentzian:
        {
            double mean = Param(component, 0);
            double gamma = Param(component, 1, 1.0);
            double z = (x - mean) / gamma;
            return 1.0 / (1.0 + z * z);
        }
        case ShapeKind::Constant:
            return 1.0;
        case ShapeKind::Linear:
            return 1.0 + Param(component, 0) * x;
        case ShapeKind::Quadratic:
            return 1.0 + Param(component, 0) * x + Param(component, 1) * x * x;
        case ShapeKind::Exponential:
            return std::exp(Param(component, 0) * x);
        case ShapeKind::Voigt:
        case ShapeKind::Custom:
            return 0.0; // not implemented yet
    }
    return 0.0;
}

double ShapeIntegral(const FitComponent& component, double a, double b)
{
    switch (component.shape)
    {
        case ShapeKind::Gaussian:
        {
            double mean = Param(component, 0);
            double sigma = Param(component, 1, 1.0);
            double za = (a - mean) / (sigma * kSqrt2);
            double zb = (b - mean) / (sigma * kSqrt2);
            return sigma * kSqrtHalfPi * (std::erf(zb) - std::erf(za));
        }
        case ShapeKind::Lorentzian:
        {
            double mean = Param(component, 0);
            double gamma = Param(component, 1, 1.0);
            return gamma * (std::atan((b - mean) / gamma) - std::atan((a - mean) / gamma));
        }
        case ShapeKind::Constant:
            return b - a;
        case ShapeKind::Linear:
        {
            double slope = Param(component, 0);
            return (b - a) + slope / 2.0 * (b * b - a * a);
        }
        case ShapeKind::Quadratic:
        {
            double slope = Param(component, 0);
            double curvature = Param(component, 1);
            return (b - a) + slope / 2.0 * (b * b - a * a) + curvature / 3.0 * (b * b * b - a * a * a);
        }
        case ShapeKind::Exponential:
        {
            double slope = Param(component, 0);
            if (std::abs(slope) < 1e-12)
            {
                return b - a; // exp(slope x) ~ 1
            }
            return (std::exp(slope * b) - std::exp(slope * a)) / slope;
        }
        case ShapeKind::Voigt:
        case ShapeKind::Custom:
            return 0.0; // not implemented yet
    }
    return 0.0;
}

double ComponentValue(const FitComponent& component, const FitRange& range, double x)
{
    double norm = ShapeIntegral(component, range.min, range.max);
    if (norm <= 0.0)
    {
        return 0.0;
    }
    return component.yield.value * ShapeValue(component, x) / norm;
}

FitCurves SampleModelCurves(const FitModel& model, int pointCount)
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

    curves.total.assign(pointCount, 0.0);

    auto sampleComponent = [&](const FitComponent& component) {
        std::vector<double> values(pointCount);
        for (int i = 0; i < pointCount; ++i)
        {
            values[i] = ComponentValue(component, model.range, curves.x[i]);
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
