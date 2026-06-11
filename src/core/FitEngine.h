#ifndef GIGGLE_CORE_FIT_ENGINE_H
#define GIGGLE_CORE_FIT_ENGINE_H

#include "FitModel.h"
#include "FitResult.h"
#include "HistogramData.h"

namespace giggle {

// Fits a model to a histogram. Implemented in rootbridge; everything else
// in GIGGLE only sees this interface.
class FitEngine
{
public:
    virtual ~FitEngine() = default;

    // Runs the fit. Never throws: failures are reported through
    // FitResult::converged and FitResult::message. Called from a worker
    // thread; implementations must not touch the UI.
    virtual FitResult Fit(const HistogramData& histogram, const FitModel& model) = 0;
};

} // namespace giggle

#endif
