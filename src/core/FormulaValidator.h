#ifndef GIGGLE_CORE_FORMULA_VALIDATOR_H
#define GIGGLE_CORE_FORMULA_VALIDATOR_H

#include <functional>
#include <string>

namespace giggle {

// The verdict on a user-entered custom formula.
struct FormulaCheckResult
{
    bool valid = false;
    int parameterCount = 0; // number of [i] parameters the formula uses
    std::string message;    // why it was rejected, when invalid
};

// Checks a formula string. Implemented with ROOT's TFormula in rootbridge
// and injected into the UI by main, so the UI never depends on ROOT.
using FormulaValidator = std::function<FormulaCheckResult(const std::string&)>;

} // namespace giggle

#endif
