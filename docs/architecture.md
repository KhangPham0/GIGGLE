# Architecture

GIGGLE is organised into three layers with a single,
strictly enforced rule about which may see ROOT. If you want to change the code,
this document maps every source file and shows what a change touches.

## The three layers

The diagram below maps the four parts of GIGGLE, the main modules inside each, and
the external libraries they rely on. The pattern to notice: `ui/` depends only on
`core/` (its data and its interfaces), never on `rootbridge/`. `rootbridge/`
implements `core/`'s two interfaces and provides its formula-validation callback,
and `main.cpp` injects those into `ui/` at startup, so the UI drives ROOT code
through the interfaces without ever naming ROOT. Each part is described in detail
under [the file reference](#file-reference).

![Layered module architecture. core/ (no ROOT, no GUI) holds the data (HistogramData, FitModel, FitResult), the Shapes math, Serialization, and the interfaces SpectrumSource, FitEngine, and FormulaValidator. ui/ (GLFW + ImGui + ImPlot) holds App, the panels (Files, Plot, Fit Model, Results), and plot rendering and export. rootbridge/ holds main.cpp and the ROOT integration (RootFileSource, RootFitEngine, FormulaSupport), which implement the core interfaces. External libraries (ROOT, nativefiledialog, GLFW/ImGui/ImPlot) sit outside.](assets/architecture_v2.svg)

- **`core/`** is the ROOT-free, ImGui-free heart: plain C++ structs in namespace
  `giggle` for a histogram, a fit model, and a fit result; all of the shape
  math; and JSON/CSV serialization. It declares two abstract interfaces,
  `SpectrumSource` (read histograms) and `FitEngine` (fit), plus a
  `FormulaValidator` callback, so it never names ROOT.
- **`rootbridge/`** is the only directory that includes ROOT headers. It
  implements those interfaces with `TFile`, `ROOT::Fit::Fitter`, `TFormula`, and
  `TF1NormSum`.
- **`ui/`** is the GLFW + Dear ImGui + ImPlot application. It depends only on
  `core` (the data model and the interfaces) and the GUI libraries. It contains
  zero ROOT code.
- **`main.cpp`** is the only file that sees both sides: it constructs the
  ROOT-backed `RootFileSource`, `RootFitEngine`, and formula validator and
  injects them into the UI through the `core` interfaces.

### The one rule, and how it is enforced

> ROOT is visible only inside `rootbridge`.

This is not a convention you have to remember; it is enforced by the build. In
CMake, `giggle_rootbridge` links ROOT **`PRIVATE`**:

```cmake
target_link_libraries(giggle_rootbridge PUBLIC giggle_core
                                         PRIVATE ROOT::Core ROOT::RIO ROOT::Hist ROOT::MathCore)
```

so ROOT's include directories never propagate to anything that links
`giggle_rootbridge`. If you add `#include "TFile.h"` to a file in `core/` or
`ui/`, it fails to compile. `core` links only nlohmann/json; the `giggle`
executable links the UI libraries and `giggle_rootbridge`, but never ROOT
directly.

Note that the UI is **not** a separate library: its sources are compiled
straight into the `giggle` executable (there is no `src/ui/CMakeLists.txt`).
Only `core` and `rootbridge` are libraries.

## How a fit flows through the code

1. The user opens a file. `main.cpp`'s factory makes a `RootFileSource`; the
   Files panel calls `List()` and `Load(path)` and gets back a plain
   `HistogramData`.
2. The user builds a model on the plot and in the Fit Model panel. There is one
   shared `FitModel` instance; the plot overlay and the panel both render from
   it, so they can never disagree. The overlay curves come from
   `SampleModelCurves` in `core/Shapes.cpp`, the *same* shape functions the fit
   will use.
3. The user fits. `App` snapshots the model, then launches the fit on a worker
   thread with `std::async`, handing the engine **copies** of the histogram and
   model. The engine never touches live UI state, and the UI polls the
   `std::future<FitResult>` without blocking.
4. `RootFitEngine::Fit` builds a `TF1` whose parameters are, per component, the
   amplitude (a density) followed by that component's shape parameters, fits it
   with Minuit2/Migrad, then derives each component's in-range counts and error
   by propagating the fit covariance through the shape integrals
   (`ComponentCountsWithError`). It also runs the independent `TF1NormSum`
   cross-check. The engine **never throws**; failures come back as
   `converged = false` with a message.
5. On success, `ApplyFitResult` writes the fitted values back into the shared
   model (so the overlay updates), and the Results panel renders the
   `FitResult`.

## File reference

### `core/`: data model, math, serialization

| File | What it holds |
|---|---|
| `HistogramData.h` | The source-agnostic 1D histogram: `binEdges`, `counts`, optional per-bin `errors` (empty ⇒ use $\sqrt{\text{counts}}$), plus `BinError`, `XMin/XMax`, `IsValid`. |
| `FitModel.{h,cpp}` | `FitParameter` (value, fixed, optional bounds), `FitComponent` (label, `ShapeKind`, amplitude + parameters), `FitModel` (range, statistic, peaks, background). `FitModel.cpp` is the single source of truth mapping each shape to its serialized name and ordered parameter names. |
| `FitResult.{h,cpp}` | Everything a fit produces: per-component `counts`/`amplitude`/`parameters` (each a `ValueWithError`), `totalCounts`, the `CrossCheck` verdict, χ²/ndf, warnings, the covariance matrix, and the sampled draw curves. `ApplyFitResult` copies fitted *values* back into a model. |
| `Shapes.{h,cpp}` | The math kernel: every shape as a unit-amplitude function, its integral and FWHM, the Faddeeva/Voigt and gf3-tail evaluation, the finite-difference count-gradient machinery, bin snapping, region statistics, and the peak-guess heuristic. |
| `SpectrumSource.h` | Abstract read interface: `List()` and `Load(path)`. |
| `FitEngine.h` | Abstract fit interface: `Fit(histogram, model)`. Contract: never throws, runs on a worker thread, must not touch the UI. |
| `FormulaValidator.h` | `FormulaCheckResult` and the `std::function` typedef for validating custom formulas. |
| `Serialization.{h,cpp}` | JSON (via `nlohmann::ordered_json`) and CSV output, the `Provenance` block, and `kSchemaVersion`. |
| `Version.{h,cpp}` | `Version()`, fed the CMake project version at compile time. |

### `rootbridge/`: the ROOT layer

| File | What it does |
|---|---|
| `RootFileSource.{h,cpp}` | Opens a ROOT file read-only and implements `SpectrumSource`. Lists 1D histograms recursively (excluding `TH2`/`TH3`), reads each bin edge individually (so variable-bin axes work), and keeps stored errors only when the histogram has `Sumw2`. |
| `RootFitEngine.{h,cpp}` | Implements `FitEngine`. Builds the `TF1`, configures fixing/bounds, runs chi-square or Poisson-likelihood fits via `ROOT::Fit::Fitter` + Minuit2, extracts counts and covariance, collects warnings, and runs the `TF1NormSum` cross-check. |
| `FormulaSupport.{h,cpp}` | Compiles, caches, and validates user `TFormula` strings, and installs the custom-shape evaluator into `core`. |

### `ui/`: the application

| File | What it does |
|---|---|
| `App.{h,cpp}` | The shell: window, GUI contexts, the menu bar and shortcuts, the open file, the shared `FitModel`, the worker-thread fit, the default dock layout, and all file/preset/results/export I/O. |
| `Theme.{h,cpp}` | The (currently only) dark theme as plain data, applied to ImGui and ImPlot. Component colours come from a fixed, distinct palette. |
| `Fonts.{h,cpp}` | Loads the embedded Inter (UI) and JetBrains Mono (numbers) fonts and merges in Font Awesome icons, with no font files needed at runtime. |
| `Widgets.h` | Small shared ImGui widgets: the accent section headers and subtrees the panels are built from. |
| `PlotRendering.{h,cpp}` | The shared plot drawing (histogram stairs, the range shade, the model curves), used by both the screen panel and the offscreen exporter, so the exported figure is the same code as the screen. |
| `PlotExport.{h,cpp}` / `ImageExport.{h,cpp}` | Render a plot into an offscreen framebuffer and write a PNG (via stb). |
| `panels/FileTreePanel.{h,cpp}` | The Files tree. |
| `panels/FitModelPanel.{h,cpp}` | The model-building UI: shape combos, parameter rows, fix/bounds, the custom-formula editor, the Fit button. |
| `panels/PlotPanel.{h,cpp}` | The plot and all direct-manipulation gestures (drag handles, add-peak, range edges, context menu, bin inspector). |
| `panels/ResultsPanel.{h,cpp}` | The results summary, the cross-check verdict, the components table, the parameter dump, warnings, and export buttons. |

## The file formats

Saved files are stable enough to parse from a script. A results document looks
like:

```jsonc
{
  "schema_version": 1,
  "provenance": {
    "giggle_version": "0.1.0",
    "timestamp": "2026-06-23T14:05:30Z",   // UTC, ISO 8601
    "source_file": "/path/to/spectrum.root",
    "histogram": "spectra/gamma"
  },
  "fit": { /* the model: schema_version, fit_range, statistic, peaks[], background[] */ },
  "result": {
    "converged": true,
    "message": "fit converged",
    "chi_square": 71.2, "degrees_of_freedom": 75, "reduced_chi_square": 0.95,
    "peaks": [ { "label": "Peak 1",
                 "counts_in_range": { "value": 4983.1, "error": 78.4 },
                 "amplitude": { "value": ..., "error": ... },
                 "centroid": { "value": ..., "error": ... },
                 "fwhm": { "value": ..., "error": ... },
                 "parameters": [ { "name": "mean", "value": ..., "error": ... }, ... ] } ],
    "background": [ ... ],
    "total_counts_in_range": { "value": ..., "error": ... },
    "normsum_cross_check": { "performed": true, "agreed": true, "detail": "largest count deviation 0.3%" },
    "warnings": [ ],
    "covariance": [ [ ... ] ]
  }
}
```

A few things worth knowing:

- A **fit preset** is just the `fit` object above (the model), saved on its own.
- The **covariance** matrix is in the engine's parameter order: per component
  (peaks first, then background, in model order), the amplitude followed by that
  component's shape parameters.
- `centroid` and `fwhm` appear only for shapes that define them.
- `schema_version` is written on output (at the top level, and again inside the
  embedded `fit` model) but **not checked on load**; there is no version-mismatch
  guard yet. Bump `kSchemaVersion` in `Serialization.h` whenever the layout
  changes.
- The CSV has one row per component with the columns
  `component, shape, counts, counts_error, centroid, centroid_error, fwhm,`
  `fwhm_error, amplitude, amplitude_error, chi2, ndf, reduced_chi2, histogram,`
  `source_file, timestamp`.

## Walkthrough: adding a new shape

Say you want to add a new peak or background shape. The work is local and the
compiler will walk you through most of it:

1. **`core/FitModel.h`**: add a value to `enum class ShapeKind`.
2. **`core/FitModel.cpp`**: add a row to `ShapeTable()` with the serialized name
   and the ordered parameter names. This single table drives serialization and
   the parameter rows in the UI.
3. **`core/Shapes.cpp`**: implement the shape in `ShapeValue` (normalised to one
   at its reference point) and `ShapeIntegral` (a closed form if you have one,
   otherwise fall through to the numeric Simpson path). If it is a peak, add it
   to `PeakCentroid`/`PeakFWHM`; otherwise leave those returning `nullopt`.
4. **`ui/panels/FitModelPanel.cpp`**: add it to the peak or background shape
   combo and give it default parameter seeds.

You do **not** touch `rootbridge` for an analytic shape: the fit engine builds
its `TF1` straight from `core`'s `ShapeValue`, so the fitted function and the
preview curve are guaranteed to be the same formula. (Custom `TFormula` shapes
are the exception; they are evaluated in `rootbridge/FormulaSupport.cpp` and
reach `core` through the injected evaluator hook.)

## Threading and lifetime notes

- Fits run on a worker thread with copies of the inputs; the UI owns no shared
  mutable state during a fit.
- `TFormula` compilation and evaluation are serialized by a single mutex, and the
  formula cache is deliberately never freed (destroying a `TFormula` during
  static teardown crashes, because ROOT's interpreter is gone by then).
- Unbalanced brackets in a custom formula are rejected *before* construction,
  because ROOT's interpreter hangs forever on incomplete input.
