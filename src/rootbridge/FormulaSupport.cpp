#include "FormulaSupport.h"

#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "TFormula.h"

#include "core/Shapes.h"

namespace giggle {

namespace {

// TFormula objects are expensive to build (cling compiles them) and not
// obviously safe to share across threads, so they are cached per formula
// string and every evaluation holds the lock. Evaluations are cheap; the
// lock is uncontended in practice.
std::mutex s_formulaMutex;

// Deliberately leaked: destroying TFormula objects during static teardown
// crashes, because ROOT's interpreter is dismantled first. The cache lives
// for the whole process anyway.
std::map<std::string, std::unique_ptr<TFormula>>& FormulaCache()
{
    static auto* cache = new std::map<std::string, std::unique_ptr<TFormula>>();
    return *cache;
}

// TFormula hands the text to cling, and cling treats input with unbalanced
// brackets as *incomplete* -- it waits forever for the rest, hanging the
// process inside the TFormula constructor. Reject such strings up front.
bool BracketsBalanced(const std::string& formula)
{
    int parentheses = 0;
    int brackets = 0;
    for (char c : formula)
    {
        parentheses += c == '(' ? 1 : 0;
        parentheses -= c == ')' ? 1 : 0;
        brackets += c == '[' ? 1 : 0;
        brackets -= c == ']' ? 1 : 0;
        if (parentheses < 0 || brackets < 0)
        {
            return false;
        }
    }
    return parentheses == 0 && brackets == 0;
}

TFormula* GetOrBuildFormula(const std::string& formula)
{
    if (!BracketsBalanced(formula))
    {
        return nullptr;
    }

    auto existing = FormulaCache().find(formula);
    if (existing != FormulaCache().end())
    {
        return existing->second.get();
    }

    auto built = std::make_unique<TFormula>("giggle_custom", formula.c_str(), false);
    TFormula* result = built->IsValid() ? built.get() : nullptr;
    FormulaCache()[formula] = std::move(built);
    return result;
}

double EvaluateFormula(const FitComponent& component, double x)
{
    std::lock_guard<std::mutex> lock(s_formulaMutex);
    TFormula* formula = GetOrBuildFormula(component.formula);
    if (formula == nullptr)
    {
        return 0.0;
    }

    double parameters[16] = {};
    int count = std::min(static_cast<int>(component.parameters.size()), 16);
    for (int i = 0; i < count; ++i)
    {
        parameters[i] = component.parameters[i].value;
    }
    double xs[1] = { x };
    return formula->EvalPar(xs, parameters);
}

} // namespace

double EvaluateFormulaRaw(const std::string& formula, const double* parameters, int count, double x)
{
    std::lock_guard<std::mutex> lock(s_formulaMutex);
    TFormula* built = GetOrBuildFormula(formula);
    if (built == nullptr)
    {
        return 0.0;
    }
    double buffer[16] = {};
    for (int i = 0; i < std::min(count, 16); ++i)
    {
        buffer[i] = parameters[i];
    }
    double xs[1] = { x };
    return built->EvalPar(xs, buffer);
}

FormulaCheckResult ValidateFormula(const std::string& formula)
{
    FormulaCheckResult result;

    if (formula.empty())
    {
        result.message = "the formula is empty";
        return result;
    }

    std::lock_guard<std::mutex> lock(s_formulaMutex);
    TFormula* built = GetOrBuildFormula(formula);
    if (built == nullptr)
    {
        result.message = "the formula does not compile";
        return result;
    }

    int parameterCount = built->GetNpar();
    if (parameterCount > 16)
    {
        result.message = "more than 16 parameters";
        return result;
    }

    // Probe values: arbitrary, nonzero, and distinct.
    std::vector<double> parameters(std::max(parameterCount, 1));
    for (int i = 0; i < parameterCount; ++i)
    {
        parameters[i] = 0.7 + 0.13 * i;
    }
    const double samples[] = { -4.1, -2.3, -0.7, 0.4, 1.1, 2.6, 3.9 };

    auto evaluate = [&](double x) {
        double xs[1] = { x };
        return built->EvalPar(xs, parameters.data());
    };

    // The formula must do something.
    double largest = 0.0;
    for (double x : samples)
    {
        largest = std::max(largest, std::abs(evaluate(x)));
    }
    if (!std::isfinite(largest) || largest < 1e-12)
    {
        result.message = "the formula evaluates to zero (or not at all) everywhere";
        return result;
    }

    // No parameter may act as a pure overall scale: if df/dp is
    // proportional to f at every sample point, scaling p is the same as
    // scaling the amplitude, and the fit cannot separate the two.
    for (int parameter = 0; parameter < parameterCount; ++parameter)
    {
        double ratio = 0.0;
        bool ratioSet = false;
        bool proportional = true;

        for (double x : samples)
        {
            double value = evaluate(x);
            double step = std::abs(parameters[parameter]) * 1e-6 + 1e-9;
            parameters[parameter] += step;
            double shifted = evaluate(x);
            parameters[parameter] -= step;
            double derivative = (shifted - value) / step;

            if (std::abs(value) < 1e-12)
            {
                // f = 0 here: a proportional derivative must vanish too.
                if (std::abs(derivative) > 1e-9 * largest)
                {
                    proportional = false;
                    break;
                }
                continue;
            }

            double thisRatio = derivative / value;
            if (!ratioSet)
            {
                ratio = thisRatio;
                ratioSet = true;
            }
            else if (std::abs(thisRatio - ratio) > 1e-6 * (1.0 + std::abs(ratio)))
            {
                proportional = false;
                break;
            }
        }

        if (proportional && ratioSet && std::abs(ratio) > 1e-9)
        {
            result.message = "[" + std::to_string(parameter) + "] only scales the formula; "
                             "remove it (the height parameter provides the scale)";
            return result;
        }
    }

    result.valid = true;
    result.parameterCount = parameterCount;
    return result;
}

void InstallCustomShapeEvaluator()
{
    SetCustomShapeEvaluator(EvaluateFormula);
}

} // namespace giggle
