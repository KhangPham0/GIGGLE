# GIGGLE

**G**raphical **I**nterface for **G**enerating **G**aussian **L**east-squares **E**stimates

GIGGLE fits one-dimensional nuclear spectra and reports the number of counts in
each peak, along with its uncertainty. It is a focused, offline companion to
[ROOT](https://root.cern): open a histogram, build a model by clicking on the
plot, fit, and read the numbers.

## Why it exists

ROOT can already fit histograms. GIGGLE focuses on getting per-peak counts,
with their uncertainties, out of a 1D spectrum, while also providing responsive
and intuitive parameter adjustment.

- **Counts come from the fit.** Each component is fit in density units; its
  counts within the fit range and their uncertainty are propagated from the fit
  covariance matrix, with correlations included.
- **Each converged fit is checked a second way.** GIGGLE re-fits the same data
  with ROOT's `TF1NormSum`, where each component's counts are *themselves* the
  fit parameters, and compares the two answers. If they disagree, it says so.
- **The plot and the model stay in sync.** Click to add a peak, drag its apex
  and width, drag the fit-range edges, press `F` to fit. The plot and the fit
  panel always show the same model.

The statistics, and the choices GIGGLE makes, are written out in
[`docs/math.md`](docs/math.md).

## Features

- **Read ROOT files.** Browse every 1D histogram in a file, including those
  nested in subdirectories and those with variable bin widths. Listing is
  instant; only the key table is read until you click a histogram.
- **A composable fit model.** Peaks plus an optional background.
  - Peak shapes: Gaussian, tailed Gaussian (gf3 / Hypermet), Lorentzian, Voigt,
    Crystal Ball, and Landau.
  - Background shapes: constant, linear, quadratic, exponential, Gaussian, and a
    smoothed (erfc) step.
  - Custom shapes from a ROOT `TFormula` string, validated before use.
- **Direct manipulation.** Hold `P` and click to add a peak; drag its apex to
  set position and height (`Shift` for height only), and the half-max handles to
  set width. Each background gets controls that suit its shape, either a level
  handle or points near the fit-range edges that you line up against the data.
  Drag the
  shaded fit-range edges, and pan, scroll-zoom, box-zoom, autoscale, use a bin
  inspector, and a log-Y toggle.
- **Results.** Per-peak counts in range (± covariance-propagated error),
  centroid, FWHM, χ²/ndf, the total model counts compared to the raw data
  counts, and the independent cross-check verdict. Warnings flag peaks that spill
  past the fit range and parameters sitting on a bound.
- **Your choice of statistic.** Chi-square (default) or Poisson likelihood
  (recommended for low-count spectra).
- **Export.** Results as pretty-printed JSON (with a provenance block
  and a schema version) or CSV; the model as a reusable JSON preset; the plot as
  a PNG at any resolution, with a headless command-line mode for batch figures.

## Quick start

You need a C++17 compiler, CMake ≥ 3.16, `git`, and an existing
[ROOT](https://root.cern) install with `root-config` on your `PATH`. OpenGL is
also needed, but it is normally already part of your system. GIGGLE is developed
and tested with ROOT 6.36.04, and other versions are untested. See
[`docs/building.md`](docs/building.md) for details, platform notes, and what to
do if a build cannot find OpenGL.

```sh
# Clone with the vendored libraries (GLFW, Dear ImGui, ImPlot, …)
git clone --recurse-submodules <repo-url> giggle
cd giggle

# Configure and build (ROOT is located automatically via root-config)
cmake -S . -B build
cmake --build build

# Run it on the bundled sample spectrum
./build/src/giggle docs/assets/sample.root
```

The sample file holds one histogram, `spectra/gamma`: a synthetic 2048-bin
spectrum in keV, made up for testing rather than measured from a real detector.
Click it in the **Files** panel on the left, then:

1. Drag the shaded fit-range edges around a peak, say the 662 keV line.
2. Hold `P` and click the peak to drop a Gaussian on it.
3. Press `F` to fit.
4. Read the counts, centroid, FWHM, and the verification verdict in the
   **Results** panel along the bottom.

A full walkthrough of every gesture and panel is in
[`docs/usage.md`](docs/usage.md).

## Documentation

| Document | What's in it |
|---|---|
| [`docs/usage.md`](docs/usage.md) | Using GIGGLE: files, the fit model, every plot gesture and shortcut, fit settings, reading results, exporting. |
| [`docs/math.md`](docs/math.md) | The statistics: the count convention, the full shape catalogue with exact formulas, where the uncertainties come from, and the independent cross-check. |
| [`docs/architecture.md`](docs/architecture.md) | How the code is organized: the three layers, the one dependency rule, a file-by-file map, and how to add a new shape. |
| [`docs/building.md`](docs/building.md) | Prerequisites, building from source, locating ROOT, and platform notes. |

## Requirements

You supply:

- A C++17 compiler (recent Clang, GCC, or Apple Clang).
- [CMake](https://cmake.org) ≥ 3.16.
- `git` (for the vendored submodules).
- [ROOT](https://root.cern), with `root-config` reachable and built with the same
  C++ standard. **You supply ROOT**; it is not vendored. Developed and tested with
  6.36.04; other versions are untested.

Also needed, but normally already present: **OpenGL** (a built-in framework on
macOS, the mesa/libGL package on Linux). If a build cannot find it,
[`docs/building.md`](docs/building.md) explains how to install it.

Everything else is vendored as git submodules and built from source:
[GLFW](https://github.com/glfw/glfw),
[Dear ImGui](https://github.com/ocornut/imgui) (docking branch),
[ImPlot](https://github.com/epezent/implot),
[nlohmann/json](https://github.com/nlohmann/json),
[nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended),
[stb](https://github.com/nothings/stb).

## Platforms

macOS and Linux are the primary targets. Windows is supported through WSL2 with
WSLg. See [`docs/building.md`](docs/building.md).

## Project layout

```
src/
├── core/         No ROOT, no ImGui. The data model (histogram, fit model,
│                 result), the shape math, and JSON/CSV serialization.
├── rootbridge/   The only code that includes ROOT. Reads files and runs fits.
├── ui/           The GLFW + Dear ImGui + ImPlot application. No ROOT.
└── main.cpp      Wires the ROOT-backed pieces into the UI.
vendor/           Third-party libraries, as git submodules.
docs/             This documentation and a sample spectrum.
```

## Status

GIGGLE is at version 0.1.0 and under active development. The fitting, the file
viewer, the full shape catalogue, and the export paths all work; the interface
and the file formats may still change before a 1.0 release.

## Acknowledgements

GIGGLE grew out of earlier spectrum-fitting work built on
[Specter](https://github.com/gwm17/Specter) (gwm17), which itself follows the
application-shell pattern of [Hazel](https://github.com/TheCherno/Hazel). It
stands on [ROOT](https://root.cern), [Dear ImGui](https://github.com/ocornut/imgui),
[ImPlot](https://github.com/epezent/implot), and the other libraries listed
above.

## License

GIGGLE is released under the MIT license; see [`LICENSE`](LICENSE). ROOT, which
you supply yourself, is distributed under LGPL-2.1, and each vendored library
keeps its own (permissive) license under `vendor/*/`.
