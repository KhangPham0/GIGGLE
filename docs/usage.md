# Using GIGGLE

This is the full walkthrough, from opening a file to exporting results. For the
statistics behind the numbers, see [`docs/math.md`](math.md); for how the code
is put together, [`docs/architecture.md`](architecture.md).

Throughout, **`Mod`** means `Cmd` on macOS and `Ctrl` elsewhere.

## The layout

GIGGLE opens as four docked panels:

- **Files** (left): the histograms in the open ROOT file.
- **Plot** (centre): the spectrum, the model overlay, and all direct
  manipulation.
- **Fit Model** (right): build and edit the model (range, peaks, background,
  fit settings).
- **Results** (bottom): counts, centroid, FWHM, χ²/ndf, the verification
  verdict, warnings, and export.

The panels are dockable Dear ImGui windows: drag a tab to rearrange or undock
them, drag a border to resize. Toggle the side panels with the **View** menu or
the shortcuts below. **View → Reset layout** restores the default arrangement.

The window layout is saved to an `imgui.ini` file kept **next to the
executable** rather than in your home directory. Delete that file, or use Reset
layout, to start fresh.

## Opening and browsing files

Open a file with **File → Open…**, with the *Open File…* button in an empty
Files panel, or by passing a path on the command line
(`giggle path/to/file.root`). The dialog is filtered to `.root` files.

The Files panel then lists the one-dimensional histograms in the file;
two- and three-dimensional histograms are not shown, and histograms with
variable bin widths are supported. When the file groups its histograms into
subdirectories, those subdirectories appear as collapsible folders that are
expanded by default. A file whose histograms are all at the top level is shown
as a plain list, with no folders.

Click a histogram to load it into the plot. When a histogram's stored title
differs from the name shown in the list, hovering over the entry displays the
full title.

Loading a histogram clears any previous fit results but keeps the current fit model, so
the same initial guess peaks can be reused across several histograms in one file. The fit range is set to the full axis only when no range has been chosen yet.

## Building a fit model

The model is a list of **peaks** plus a single optional **background**. The Fit
Model panel and the plot edit the *same* model, so they can never disagree.

### The fit range

The **Range** section has `min` and `max` fields (drag to adjust, double-click
to type). Every edit snaps to the nearest bin edges, because a fit selects whole
bins. If the range is empty (`max ≤ min`) the panel says so in amber and the
Fit button is disabled. You can also set the range by dragging its shaded edges
on the plot, or from the plot's right-click menu.

### Peaks

Add a peak in any of three ways:

- **Hold `P` and click** the peak on the plot.
- Turn on **Add peaks on click** (plot right-click menu) and click as many as
  you like; press `Esc` to leave the mode.
- Click **Add Peak** in the Fit Model panel (drops one at the range centre).

A new peak is seeded by guessing from the data near the click: the mean from the
tallest nearby bin, the width from a half-max scan, and the height above the
local baseline.

Each peak has a **shape** selector with a row per parameter. The options are
`gaussian`, `gaussian_tail` (the tailed Gaussian), `lorentzian`, `voigt`,
`crystal_ball`, `landau`, and `custom`. Switching shapes carries the mean and a
comparable width across, so you can try a different lineshape on the same peak
without starting over. Every shape's exact formula is in
[`docs/math.md`](math.md#the-shape-catalogue).

### Background

The **Background** section offers a single component: None, or one of
`constant`, `linear`, `quadratic`, `exponential`, `gaussian`, `step`, and
`custom`. Choosing **None** removes it; choosing a new shape replaces the old one
and seeds sensible defaults (a flat level at about 20% of the tallest in-range
bin).

### Heights, parameters, fixing, and bounds

- The amplitude row is labelled **height** for peaks and **level** for a
  background, and it is shown **in counts, as read off the plot**. GIGGLE
  converts the internal density to counts using the local bin width for you.
  (Custom shapes show the raw amplitude.)
- Every parameter has a **fix** checkbox that holds it constant during the fit.
  A fixed parameter shows a small lock glyph next to its name, and its on-plot
  handle stops responding to drags, though you can still type a new value in the
  field. Fixing freezes the fit and the drag handle, not deliberate edits.
- **Right-click any value** to set a lower and/or upper **bound**. Bounded
  fields are tinted; hovering an unbounded field reminds you that right-click
  sets bounds, and hovering a bounded one shows the active bounds. The peak
  height, the background level, and the widths (`sigma` and `gamma`) are bounded
  to be non-negative by default, so a fit cannot drive them below zero.
- Drag a value to adjust it, or double-click to type.

### Custom shapes

Pick **Custom** and type a formula in `x` using parameters `[0]`, `[1]`, and so
on, then press **Apply**. GIGGLE compiles the formula through ROOT and reports
how many parameters it found, or why it was rejected. A formula is rejected if
it is empty, does not compile, has unbalanced brackets, uses more than 16
parameters, evaluates to zero everywhere, or contains a parameter that only
scales the whole formula. The height (for a peak) or level (for a background)
already supplies that scale, so an overall-scale parameter would be redundant
with it.

The formula defines the *shape*; the height or level then multiplies it, exactly
as it does for the built-in shapes. Write the shape so its parameters control
its form rather than its overall size, and set starting values in the parameter
rows that appear once the formula is accepted.

A custom **peak**. As an example, a pseudo-Voigt mixes a Gaussian and a
Lorentzian of the same width:

```
(1 - [2]) * exp(-0.5 * ((x - [0]) / [1])^2) + [2] / (1 + ((x - [0]) / [1])^2)
```

`[0]` is the centre, `[1]` the width, and `[2]` the Lorentzian fraction (between
0 and 1). The expression equals 1 at the centre, so the height is the peak
height.

A custom **background**. As an example, a logistic turn-on rises smoothly from
zero to a plateau:

```
1 / (1 + exp(([0] - x) / [1]))
```

`[0]` is the position of the edge and `[1]` its width; the level sets the height
of the plateau.

## Working in the plot

The spectrum is drawn as translucent filled stairs. The fitted total is the
bright accent curve, and the individual components are drawn in dimmer colours.

Once a histogram is loaded, a **Fit tools** button appears at the top-left of the
plot. It shows or hides the fit overlay (the range shade, the handles, and the
model curves), the same toggle as **Fit tools** in the right-click menu, and it
is highlighted while the overlay is on. Turn it off for a clean view of the
spectrum, which is also how an exported figure looks.

### Navigation

| Gesture | Action |
|---|---|
| Scroll | Zoom (at the cursor) |
| Right-drag | Box zoom (hold `Shift` for x-only) |
| Double-click | Autoscale to the data |
| Left-drag | Pan |

### Editing on the plot

Every visible component has draggable handles, and they always sit on the curve
they control. Toggling a component off in the legend (top-right) hides its
handles too. A parameter you have fixed has an inert handle (and a lock glyph in
the panel).

- **Fit-range edges**: drag the two shaded edges. They snap to bin edges and
  swap if you drag one past the other.
- **Peak apex**: drag to move the mean (horizontal) and set the height
  (vertical). Hold **`Shift`** to change only the height, keeping the centroid
  where it is, which helps when the position is already right. This is a per-drag
  lock; it does not fix the mean for the fit.
- **Half-max handles**: two handles on each peak set the width (FWHM) directly.
  On the asymmetric tailed Gaussian and Landau they scale the width
  proportionally rather than moving a single side.
- **Background controls** depend on the shape:
  - *Constant, step, custom*: one level handle on the curve.
  - *Linear, exponential*: two points near the fit-range edges. Drag each to
    line it up with the data at that end; together they set the level and the
    slope.
  - *Quadratic*: the same two edge points plus a middle point. The edges fix the
    ends, and dragging the middle up or down against the line between them sets
    the curvature, both its direction (concave up or down) and its size.
  - *Gaussian*: the peak controls (apex and half-max), since a Gaussian
    background is just a peak used as a background.

### The right-click menu

A right-*click* (not a right-drag, which is box zoom) opens a context menu at
that position:

- **Fit tools** (show or hide the whole fit overlay)
- Add peak here · Set range start here · Set range end here
- Add peaks on click · Bin inspector · Log scale Y · Autoscale axes
- Fit (`F`) · Save plot as PNG…
- a reminder of the gestures (box zoom, scroll zoom, hold-`P`)

**Fit tools** toggles the entire fit overlay (the range shade, the handles, and
the model curves); turning it off leaves just the spectrum. **Bin inspector**
highlights the bin under the cursor and shows its index, edges, and counts with
its error. **Log scale Y** switches the vertical axis to log.

## Fitting

Press **`F`**, click **Fit** in the Fit Model panel, or choose **Fit** from the
plot menu. The fit runs on a background thread, so the interface never freezes;
the button shows *Fitting…* while it works.

In **Fit Settings**, choose the statistic: **Chi-square** (default) or **Poisson
likelihood** (better for low-count spectra). See
[`docs/math.md`](math.md#goodness-of-fit-which-statistic) for the difference.

When the fit converges, the fitted values are written back into the model (so the
overlay updates) and the Results panel fills in. **Revert to pre-fit** restores
the model to its state just before the fit, a one-step undo.

## A worked example: a doublet on a sloped background

Two overlapping peaks on a falling background, the case the single-peak quick
start does not cover:

1. **Frame the region.** Drag the shaded fit-range edges to bracket both peaks,
   leaving a little background on each side. They snap to bin boundaries.
2. **Drop the peaks.** Turn on **Add peaks on click** (plot right-click menu),
   click each peak once, and press `Esc` to leave the mode. Each is seeded from
   the data under the click.
3. **Add the background.** In the Fit Model panel set **Background → linear**.
   Two control points appear near the range edges.
4. **Line up the background.** Drag the left and right points down onto the
   baseline at each edge. The two points set the level and the slope together,
   so the line follows the data along the shelf.
5. **Tidy the peaks.** Drag each apex onto its peak (hold `Shift` if only the
   height needs work) and pull the half-max handles to roughly the right width.
   Good starting guesses help the fit converge.
6. **Fit.** Press `F`; the overlay updates and the Results panel fills in.
7. **Read and check.** Each peak's counts (± error) and a green *counts
   independently verified* line tell you the answer and that it is trustworthy.
   If a peak's core spills past the range, widen it and refit.

## Reading the results

The Results panel always shows the **data in range**, which is the raw counts in
the current range together with the data centroid and rms, whether or not you
have run a fit. Once a fit exists, it adds the **model total** counts for
comparison.

- A green **Converged** or red **Failed** line, with `χ²/ndf` and the reduced
  value.
- The **verification verdict**: green *counts independently verified* (with the
  largest deviation), red *VERIFICATION FAILED (…) - do not trust these counts*,
  or a note that the cross-check was not performed (variable bins or a fixed
  height). This is the independent `TF1NormSum` re-fit described in
  [`docs/math.md`](math.md#the-independent-cross-check).
- A **components table**: per component, the counts in range (± error), the
  centroid, and the FWHM (blank where a shape does not define one).
- An **All parameters** tree with every fitted value and error (heights shown in
  plot counts).
- **Warnings** in amber, for example a peak whose core extends past the fit
  range, or a parameter sitting on a bound (where the parabolic error is
  unreliable).

## Exporting

- **Results → Save JSON…** writes a pretty-printed JSON document: the schema
  version, a provenance block (GIGGLE version, UTC timestamp, source file,
  histogram), the full model, and the results including the covariance matrix.
  The format is described in
  [`docs/architecture.md`](architecture.md#the-file-formats).
- **Results → Save CSV…** / **Copy CSV** writes a flat table, one row per
  component, for spreadsheets or pandas.
- **File → Export Plot as PNG…** renders the *currently framed* view at a
  resolution you choose (presets 1920×1080, 2560×1440, 3840×2160), with an
  automatic or manual line/text emphasis so high-resolution figures don't come
  out hairline-thin. The export reproduces the on-screen plot exactly, including
  the log/linear Y setting.
- **File → Save Fit Preset…** / **Load Fit Preset…** stores the whole model as
  JSON and reloads it later. A loaded preset's range re-snaps to the current
  histogram's bins, so presets are reusable across spectra.

### Headless plot export

For batch figures you can render a plot without opening the window:

```sh
giggle file.root --export-plot spectra/gamma out.png
```

This loads the named histogram, lets the axes settle, writes a 1920×1080 PNG,
and exits.

## Menu and shortcut reference

**File**: Open… · Save Fit Preset… · Load Fit Preset… · Export Plot as PNG… ·
Quit. (Preset and export items are enabled once a histogram is open.)

**View**: toggle Files (`Mod+B`), Fit Model (`Mod+J`), Results · Larger /
Smaller / Reset text size · Reset layout.

**About**: About GIGGLE.

| Shortcut | Action |
|---|---|
| `F` | Run the fit |
| `Mod+B` | Toggle the Files panel |
| `Mod+J` | Toggle the Fit Model panel |
| `Mod+=` / `Mod+-` | Larger / smaller text |
| `Mod+0` | Reset text size |
| `P` (hold) + click | Add a peak at the cursor |
| `Esc` | Leave "add peaks on click" mode |

Shortcuts are ignored while you are typing in a text field.
