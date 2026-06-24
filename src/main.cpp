// GIGGLE: fit 1D spectra, extract per-peak counts and uncertainties.
//
// This is the only file that sees both sides of the ROOT boundary: it hands
// the ROOT-backed file reader to the UI through the SpectrumSource
// interface.

#include <memory>
#include <string>

#include "rootbridge/FormulaSupport.h"
#include "rootbridge/RootFileSource.h"
#include "rootbridge/RootFitEngine.h"
#include "ui/App.h"

int main(int argc, char** argv)
{
    // Custom shapes are evaluated through ROOT's TFormula; this hook lets
    // core preview and integrate them while staying ROOT-free itself.
    giggle::InstallCustomShapeEvaluator();

    giggle::App app(
        [](const std::string& filePath) -> std::unique_ptr<giggle::SpectrumSource> {
            return std::make_unique<giggle::RootFileSource>(filePath);
        },
        std::make_unique<giggle::RootFitEngine>(),
        giggle::ValidateFormula);

    // Optionally open a file straight away: giggle path/to/file.root
    if (argc > 1)
    {
        app.OpenFileOnStartup(argv[1]);
    }

    // Scripted export: giggle file.root --export-plot histogram out.png
    if (argc > 4 && std::string(argv[2]) == "--export-plot")
    {
        app.ExportOnStartup(argv[3], argv[4]);
    }

    return app.Run();
}
