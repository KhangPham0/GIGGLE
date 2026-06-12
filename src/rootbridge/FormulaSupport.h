#ifndef GIGGLE_ROOTBRIDGE_FORMULA_SUPPORT_H
#define GIGGLE_ROOTBRIDGE_FORMULA_SUPPORT_H

#include "core/FormulaValidator.h"

namespace giggle {

// Checks a custom formula: it must compile as a ROOT TFormula, evaluate to
// something nonzero, and contain no parameter acting as a pure overall
// scale (such a parameter would be degenerate with the component's
// amplitude and break the fit).
FormulaCheckResult ValidateFormula(const std::string& formula);

// Installs the TFormula-backed evaluator for custom shapes into core, so
// previews, integrals, and count extraction work for them. Call once at
// startup.
void InstallCustomShapeEvaluator();

// Evaluates a formula with an explicit parameter array; the fit engine's
// inner loop uses this form.
double EvaluateFormulaRaw(const std::string& formula, const double* parameters, int count, double x);

} // namespace giggle

#endif
