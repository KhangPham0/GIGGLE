#ifndef GIGGLE_ROOTBRIDGE_ROOT_FIT_ENGINE_H
#define GIGGLE_ROOTBRIDGE_ROOT_FIT_ENGINE_H

#include "core/FitEngine.h"

namespace giggle {

// Fits models with ROOT (Minuit2 through TH1::Fit).
//
// The fit function is built directly from the shape math in core/Shapes, so
// the fitted function and the preview curves are the same formulas by
// construction. Every yield is a fit parameter (the in-range count), so
// counts and their uncertainties come straight from the fit covariance.
class RootFitEngine : public FitEngine
{
public:
    RootFitEngine();

    FitResult Fit(const HistogramData& histogram, const FitModel& model) override;
};

} // namespace giggle

#endif
