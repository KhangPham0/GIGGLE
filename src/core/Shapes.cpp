#include "Shapes.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace giggle {

namespace {

const double kSqrt2 = std::sqrt(2.0);
const double kSqrtHalfPi = std::sqrt(M_PI / 2.0);

CustomShapeEvaluator s_customShapeEvaluator;

// The i-th parameter, or fallback when fewer are given.
double Param(const double* parameters, int count, int index, double fallback = 0.0)
{
    return index < count ? parameters[index] : fallback;
}

// Humlicek's w4 rational approximation of the Faddeeva function w(z) for
// Im(z) >= 0; relative accuracy about 1e-4 (Humlicek, JQSRT 27, 437, 1982).
std::complex<double> FaddeevaW(double x, double y)
{
    std::complex<double> t(y, -x);
    double s = std::abs(x) + y;

    if (s >= 15.0)
    {
        return t * 0.5641896 / (0.5 + t * t);
    }
    if (s >= 5.5)
    {
        std::complex<double> u = t * t;
        return t * (1.410474 + u * 0.5641896) / (0.75 + u * (3.0 + u));
    }
    if (y >= 0.195 * std::abs(x) - 0.176)
    {
        return (16.4955 + t * (20.20933 + t * (11.96482 + t * (3.778987 + t * 0.5642236))))
               / (16.4955 + t * (38.82363 + t * (39.27121 + t * (21.69274 + t * (6.699398 + t)))));
    }
    std::complex<double> u = t * t;
    return std::exp(u)
           - t * (36183.31 - u * (3321.9905 - u * (1540.787 - u * (219.0313 - u * (35.76683 - u * (1.320522 - u * 0.56419))))))
                 / (32066.6 - u * (24322.84 - u * (9022.228 - u * (2186.181 - u * (364.2191 - u * (61.57037 - u * (1.841439 - u)))))));
}

// The Voigt profile relative to its central value, so it is 1 at u = 0.
// u = x - mean. Falls back to the pure limits when one width vanishes.
double VoigtShape(double u, double sigma, double gamma)
{
    sigma = std::abs(sigma);
    gamma = std::abs(gamma);
    if (sigma < 1e-12)
    {
        double z = gamma > 1e-12 ? u / gamma : 0.0;
        return 1.0 / (1.0 + z * z); // Lorentzian limit
    }
    if (gamma < 1e-12 * sigma)
    {
        double z = u / sigma;
        return std::exp(-0.5 * z * z); // Gaussian limit
    }
    double real = u / (sigma * kSqrt2);
    double imaginary = gamma / (sigma * kSqrt2);
    double center = FaddeevaW(0.0, imaginary).real();
    return FaddeevaW(real, imaginary).real() / center;
}

// The skewed-gaussian tail term of the gf3 lineshape:
//
//   T(u) = exp(u / beta) * erfc(u / (sqrt2 sigma) + k),  k = sigma / (sqrt2 beta)
//
// For u well above the mean the exp overflows while the erfc underflows;
// their product decays like a gaussian, so that region uses the erfc
// asymptotic erfc(t) ~ exp(-t^2) / (t sqrt pi) directly.
double TailTerm(double u, double sigma, double beta)
{
    double v = u / (kSqrt2 * sigma);
    double k = sigma / (kSqrt2 * beta);
    double t = v + k;
    if (t > 6.0)
    {
        return std::exp(-k * k - v * v) / (t * std::sqrt(M_PI));
    }
    return std::exp(u / beta) * std::erfc(t);
}

// Antiderivative of TailTerm (checked by differentiation):
//
//   F(u) = beta * [ T(u) + exp(-k^2) * erf(u / (sqrt2 sigma)) ]
double TailTermAntiderivative(double u, double sigma, double beta)
{
    double v = u / (kSqrt2 * sigma);
    double k = sigma / (kSqrt2 * beta);
    return beta * (TailTerm(u, sigma, beta) + std::exp(-k * k) * std::erf(v));
}

// The gf3 composite, scaled to 1 at the mean:
//
//   shape(u) = [ (1 - r) exp(-u^2 / 2 sigma^2) + r T(u) ] / N0
//   N0       = (1 - r) + r erfc(k)
//
// r clamped to [0, 1]; widths floored to stay finite.
struct TailShapeParts
{
    double sigma;
    double beta;
    double fraction;
    double norm;
};

TailShapeParts TailParts(const double* p, int count)
{
    TailShapeParts parts;
    parts.sigma = std::max(std::abs(Param(p, count, 1, 1.0)), 1e-12);
    parts.fraction = std::min(std::max(Param(p, count, 2), 0.0), 1.0);
    parts.beta = std::max(std::abs(Param(p, count, 3, 1.0)), 1e-12);
    double k = parts.sigma / (kSqrt2 * parts.beta);
    parts.norm = (1.0 - parts.fraction) + parts.fraction * std::erfc(k);
    if (parts.norm < 1e-300)
    {
        parts.norm = 1e-300; // fully tailed with a vanishing tail term
    }
    return parts;
}

// Composite Simpson integration for the shapes without a closed form.
double SimpsonIntegral(const std::function<double(double)>& f, double a, double b)
{
    const int intervals = 200; // even; smooth integrands converge fast
    double h = (b - a) / intervals;
    double sum = f(a) + f(b);
    for (int i = 1; i < intervals; ++i)
    {
        sum += f(a + i * h) * (i % 2 == 0 ? 2.0 : 4.0);
    }
    return sum * h / 3.0;
}

// The standard Landau density phi(lambda), via the Kolbig-Schorr rational
// approximation (CERNLIB G110 DENLAN) -- the same one ROOT's TMath::Landau
// uses, reimplemented here so core stays free of ROOT. Peaks near
// lambda = -0.2228 with a heavy tail toward positive lambda.
double LandauDensity(double v)
{
    static const double p1[] = { 0.4259894875, -0.1249762550, 0.03984243700, -0.006298287635, 0.001511162253 };
    static const double q1[] = { 1.0, -0.3388260629, 0.09594393323, -0.01608042283, 0.003778942063 };
    static const double p2[] = { 0.1788541609, 0.1173957403, 0.01488850518, -0.001394989411, 0.0001283617211 };
    static const double q2[] = { 1.0, 0.7428795082, 0.3153932961, 0.06694219548, 0.008790609714 };
    static const double p3[] = { 0.1788544503, 0.09359161662, 0.006325387654, 0.00006611667319, -0.000002031049101 };
    static const double q3[] = { 1.0, 0.6097809921, 0.2560616665, 0.04746722384, 0.006957301675 };
    static const double p4[] = { 0.9874054407, 118.6723273, 849.2794360, -743.7792444, 427.0262186 };
    static const double q4[] = { 1.0, 106.8615961, 337.6496214, 2016.712389, 1597.063511 };
    static const double p5[] = { 1.003675074, 167.5702434, 4789.711289, 21217.86767, -22324.94910 };
    static const double q5[] = { 1.0, 156.9424537, 3745.310488, 9834.698876, 66924.28357 };
    static const double p6[] = { 1.000827619, 664.9143136, 62972.92665, 475554.6998, -5743609.109 };
    static const double q6[] = { 1.0, 651.4101098, 56974.73333, 165917.4725, -2815759.939 };
    static const double a1[] = { 0.04166666667, -0.01996527778, 0.02709538966 };
    static const double a2[] = { -1.845568670, -4.284640743 };

    if (v < -5.5)
    {
        double u = std::exp(v + 1.0);
        if (u < 1e-10)
        {
            return 0.0;
        }
        return 0.3989422803 * std::exp(-1.0 / u) / std::sqrt(u)
               * (1.0 + (a1[0] + (a1[1] + a1[2] * u) * u) * u);
    }
    if (v < -1.0)
    {
        double u = std::exp(-v - 1.0);
        return std::exp(-u) * std::sqrt(u)
               * (p1[0] + (p1[1] + (p1[2] + (p1[3] + p1[4] * v) * v) * v) * v)
               / (q1[0] + (q1[1] + (q1[2] + (q1[3] + q1[4] * v) * v) * v) * v);
    }
    if (v < 1.0)
    {
        return (p2[0] + (p2[1] + (p2[2] + (p2[3] + p2[4] * v) * v) * v) * v)
               / (q2[0] + (q2[1] + (q2[2] + (q2[3] + q2[4] * v) * v) * v) * v);
    }
    if (v < 5.0)
    {
        return (p3[0] + (p3[1] + (p3[2] + (p3[3] + p3[4] * v) * v) * v) * v)
               / (q3[0] + (q3[1] + (q3[2] + (q3[3] + q3[4] * v) * v) * v) * v);
    }
    if (v < 12.0)
    {
        double u = 1.0 / v;
        return u * u * (p4[0] + (p4[1] + (p4[2] + (p4[3] + p4[4] * u) * u) * u) * u)
               / (q4[0] + (q4[1] + (q4[2] + (q4[3] + q4[4] * u) * u) * u) * u);
    }
    if (v < 50.0)
    {
        double u = 1.0 / v;
        return u * u * (p5[0] + (p5[1] + (p5[2] + (p5[3] + p5[4] * u) * u) * u) * u)
               / (q5[0] + (q5[1] + (q5[2] + (q5[3] + q5[4] * u) * u) * u) * u);
    }
    if (v < 300.0)
    {
        double u = 1.0 / v;
        return u * u * (p6[0] + (p6[1] + (p6[2] + (p6[3] + p6[4] * u) * u) * u) * u)
               / (q6[0] + (q6[1] + (q6[2] + (q6[3] + q6[4] * u) * u) * u) * u);
    }
    double u = 1.0 / (v - v * std::log(v) / (v + 1.0));
    return u * u * (1.0 + (a2[0] + a2[1] * u) * u);
}

// The Landau density's peak value, used to normalize the shape to 1 there.
const double kLandauPeakLambda = -0.22278298;
const double kLandauPeakValue = LandauDensity(kLandauPeakLambda);

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
    return kind != ShapeKind::Custom;
}

void SetCustomShapeEvaluator(CustomShapeEvaluator evaluator)
{
    s_customShapeEvaluator = std::move(evaluator);
}

bool HasCustomShapeEvaluator()
{
    return static_cast<bool>(s_customShapeEvaluator);
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
            return VoigtShape(x - Param(p, count, 0), Param(p, count, 1, 1.0), Param(p, count, 2));
        case ShapeKind::CrystalBall:
        {
            // Gaussian core; below t = -alpha it switches to a power-law
            // tail that joins the core smoothly. The core is 1 at the mean.
            double mean = Param(p, count, 0);
            double sigma = std::max(std::abs(Param(p, count, 1, 1.0)), 1e-12);
            double alpha = std::max(std::abs(Param(p, count, 2, 1.0)), 1e-6);
            double n = std::max(Param(p, count, 3, 2.0), 1.0 + 1e-6);
            double t = (x - mean) / sigma;
            if (t > -alpha)
            {
                return std::exp(-0.5 * t * t);
            }
            double aCoef = std::pow(n / alpha, n) * std::exp(-0.5 * alpha * alpha);
            double bCoef = n / alpha - alpha;
            return aCoef * std::pow(bCoef - t, -n);
        }
        case ShapeKind::Landau:
        {
            // Placed so the density's peak sits at the mean, and normalized
            // to 1 there. The heavy tail runs to the high-energy side.
            double mean = Param(p, count, 0);
            double scale = std::max(std::abs(Param(p, count, 1, 1.0)), 1e-12);
            // Shift the location so the density's peak (at kLandauPeakLambda)
            // lands at the mean, then normalize to 1 there.
            double location = mean - kLandauPeakLambda * scale;
            double lambda = (x - location) / scale;
            return LandauDensity(lambda) / kLandauPeakValue;
        }
        case ShapeKind::GaussianTail:
        {
            TailShapeParts parts = TailParts(p, count);
            double u = x - Param(p, count, 0);
            double z = u / parts.sigma;
            return ((1.0 - parts.fraction) * std::exp(-0.5 * z * z)
                    + parts.fraction * TailTerm(u, parts.sigma, parts.beta))
                   / parts.norm;
        }
        case ShapeKind::Step:
        {
            double edge = Param(p, count, 0);
            double width = std::max(std::abs(Param(p, count, 1, 1.0)), 1e-12);
            return 0.5 * std::erfc((x - edge) / (kSqrt2 * width));
        }
        case ShapeKind::Custom:
            return 0.0; // needs the component's formula; see the FitComponent overload
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
        case ShapeKind::CrystalBall:
        case ShapeKind::Landau:
            return SimpsonIntegral(
                [&](double x) { return ShapeValue(kind, p, count, x, pivot); }, a, b);
        case ShapeKind::GaussianTail:
        {
            TailShapeParts parts = TailParts(p, count);
            double mean = Param(p, count, 0);
            double ua = a - mean;
            double ub = b - mean;
            double gaussPart = parts.sigma * kSqrtHalfPi
                               * (std::erf(ub / (parts.sigma * kSqrt2))
                                  - std::erf(ua / (parts.sigma * kSqrt2)));
            double tailPart = TailTermAntiderivative(ub, parts.sigma, parts.beta)
                              - TailTermAntiderivative(ua, parts.sigma, parts.beta);
            return ((1.0 - parts.fraction) * gaussPart + parts.fraction * tailPart) / parts.norm;
        }
        case ShapeKind::Step:
        {
            double edge = Param(p, count, 0);
            double width = std::max(std::abs(Param(p, count, 1, 1.0)), 1e-12);
            // Antiderivative of erfc(t)/2 with t = (x - edge)/(sqrt2 width):
            //   (width/sqrt2) * (t erfc(t) - exp(-t^2)/sqrt(pi))
            auto antiderivative = [&](double x) {
                double t = (x - edge) / (kSqrt2 * width);
                return (width / kSqrt2) * (t * std::erfc(t) - std::exp(-t * t) / std::sqrt(M_PI));
            };
            return antiderivative(b) - antiderivative(a);
        }
        case ShapeKind::Custom:
            return 0.0; // needs the component's formula; see the FitComponent overload
    }
    return 0.0;
}

double ShapeValue(const FitComponent& component, const FitRange& range, double x)
{
    if (component.shape == ShapeKind::Custom)
    {
        return s_customShapeEvaluator ? s_customShapeEvaluator(component, x) : 0.0;
    }
    double parameters[8];
    int count = FlattenParameters(component, parameters, 8);
    return ShapeValue(component.shape, parameters, count, x, RangePivot(range));
}

double ShapeIntegral(const FitComponent& component, const FitRange& range, double a, double b)
{
    if (component.shape == ShapeKind::Custom)
    {
        if (!s_customShapeEvaluator)
        {
            return 0.0;
        }
        return SimpsonIntegral(
            [&](double x) { return s_customShapeEvaluator(component, x); }, a, b);
    }
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
        case ShapeKind::GaussianTail:
        case ShapeKind::Lorentzian:
        case ShapeKind::Voigt:
        case ShapeKind::CrystalBall:
        case ShapeKind::Landau:
            // The first parameter is the mean. (For the tailed gaussian,
            // crystal ball, and landau this is the position of the peak, as
            // in gf3, not the center of gravity of the asymmetric profile.)
            if (!result.parameters.empty())
            {
                return result.parameters[0];
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

namespace {

// The distance from the mean to the half-maximum crossing in the given
// direction, found by expansion and bisection on the shape itself.
double HalfMaxDistance(ShapeKind kind, const double* p, int count, double direction)
{
    double mean = count > 0 ? p[0] : 0.0;
    double scale = count > 1 ? std::max(std::abs(p[1]), 1e-9) : 1.0;

    double inside = 0.0;
    double outside = scale;
    int expansions = 0;
    while (ShapeValue(kind, p, count, mean + direction * outside, 0.0) > 0.5 && expansions < 60)
    {
        inside = outside;
        outside *= 2.0;
        ++expansions;
    }
    for (int i = 0; i < 60; ++i)
    {
        double middle = 0.5 * (inside + outside);
        if (ShapeValue(kind, p, count, mean + direction * middle, 0.0) > 0.5)
        {
            inside = middle;
        }
        else
        {
            outside = middle;
        }
    }
    return 0.5 * (inside + outside);
}

// FWHM of an asymmetric shape by locating both half-max crossings; the
// error perturbs each parameter by its uncertainty (correlations between
// the shape parameters are neglected, as for the Voigt approximation).
ValueWithError NumericFWHM(ShapeKind kind, const ComponentResult& result)
{
    double parameters[8];
    int count = std::min(static_cast<int>(result.parameters.size()), 8);
    for (int i = 0; i < count; ++i)
    {
        parameters[i] = result.parameters[i].value;
    }

    auto fullWidth = [&]() {
        return HalfMaxDistance(kind, parameters, count, -1.0)
               + HalfMaxDistance(kind, parameters, count, +1.0);
    };

    ValueWithError fwhm;
    fwhm.value = fullWidth();

    double variance = 0.0;
    for (int i = 0; i < count; ++i)
    {
        if (result.parameters[i].error <= 0.0)
        {
            continue;
        }
        double original = parameters[i];
        parameters[i] = original + result.parameters[i].error;
        double shifted = fullWidth();
        parameters[i] = original;
        variance += (shifted - fwhm.value) * (shifted - fwhm.value);
    }
    fwhm.error = std::sqrt(variance);
    return fwhm;
}

} // namespace

std::optional<ValueWithError> PeakFWHM(ShapeKind kind, const ComponentResult& result)
{
    switch (kind)
    {
        case ShapeKind::GaussianTail:
            if (result.parameters.size() >= 4)
            {
                return NumericFWHM(kind, result);
            }
            return std::nullopt;
        case ShapeKind::CrystalBall:
            // Asymmetric (power-law tail), so the width is found numerically.
            if (result.parameters.size() >= 4)
            {
                return NumericFWHM(kind, result);
            }
            return std::nullopt;
        case ShapeKind::Landau:
            // Asymmetric (heavy high-energy tail); width found numerically.
            if (result.parameters.size() >= 2)
            {
                return NumericFWHM(kind, result);
            }
            return std::nullopt;
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
        case ShapeKind::Voigt:
        {
            // Olivero-Longbothum approximation (accuracy ~0.02%):
            //   F ~ 0.5346 fL + sqrt(0.2166 fL^2 + fG^2)
            // The error combines the sigma and gamma errors through the
            // formula's partial derivatives; their correlation (not
            // available here) is neglected.
            if (result.parameters.size() >= 3)
            {
                const double gaussFactor = 2.0 * std::sqrt(2.0 * std::log(2.0));
                double fG = gaussFactor * std::abs(result.parameters[1].value);
                double fL = 2.0 * std::abs(result.parameters[2].value);
                double rootTerm = std::sqrt(0.2166 * fL * fL + fG * fG);
                double fwhm = 0.5346 * fL + rootTerm;

                double dFdfL = 0.5346 + (rootTerm > 0.0 ? 0.2166 * fL / rootTerm : 0.0);
                double dFdfG = rootTerm > 0.0 ? fG / rootTerm : 1.0;
                double errorL = dFdfL * 2.0 * result.parameters[2].error;
                double errorG = dFdfG * gaussFactor * result.parameters[1].error;
                return ValueWithError{ fwhm, std::sqrt(errorL * errorL + errorG * errorG) };
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

RegionStats AnalyzeRange(const HistogramData& histogram, const FitRange& range)
{
    RegionStats stats;
    double variance = 0.0;
    double weightedX = 0.0;
    double weightedXX = 0.0;
    for (int bin = 0; bin < histogram.BinCount(); ++bin)
    {
        double center = 0.5 * (histogram.binEdges[bin] + histogram.binEdges[bin + 1]);
        if (center < range.min || center > range.max)
        {
            continue;
        }
        double weight = histogram.counts[bin];
        stats.counts.value += weight;
        double error = histogram.BinError(bin);
        variance += error * error;
        weightedX += weight * center;
        weightedXX += weight * center * center;
    }
    stats.counts.error = std::sqrt(variance);

    if (stats.counts.value > 0.0)
    {
        stats.centroid = weightedX / stats.counts.value;
        double secondMoment = weightedXX / stats.counts.value - stats.centroid * stats.centroid;
        stats.rms = secondMoment > 0.0 ? std::sqrt(secondMoment) : 0.0;
    }
    return stats;
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
