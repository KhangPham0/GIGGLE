# The math behind GIGGLE

This document explains every number GIGGLE reports and where it comes from, so
nothing is a black box. The exact formulas are here, each with a short note on
what it means.

Essentially, GIGGLE fits a spectrum: it finds the smooth curve that best matches the
histogram, then reads off how many counts belong to each peak and how uncertain
that number is. The sections below define each step.

Every formula here is exactly what the code computes. The preview you build on
the plot and the function the fit optimises come from the same shape definitions
(`src/core/Shapes.cpp`); only the parameter values differ (your initial guess,
then the fitted result). The two curves are not the same picture, but they are
never a different formula, so what you preview is exactly what gets fit.

## The fit model

The fitted curve is a sum of pieces: one per peak, plus an optional background.
Each piece is a **shape** (a Gaussian, a sloped line, and so on) times an
**amplitude** that scales it:

$$ f(x) = \sum_i A_i\,\phi_i(x) . $$

Every shape is normalised to equal 1 at its *reference point*, a peak's mean
$\mu$ or, for a background, the center of the fit range (the *pivot* $p$), so the
amplitude $A_i$ is just the curve's height there.

The curve is a density (per unit $x$) while a bin holds counts, so a bin of width
$\Delta x$ expects $f(x)\,\Delta x$ counts. That one factor lets the panel show a
peak's **height in counts** while the fit works in densities; the two differ by a
constant, so fixing or bounding the height is the same as fixing or bounding the
amplitude.

## Counts in the fit range

The headline number for each piece is its **counts in the fit range**: the area
under its curve between the range edges,

$$ N_i = A_i \int_{x_{\min}}^{x_{\max}} \phi_i(x)\,\mathrm{d}x . $$

Note what "normalised to 1" means: $\phi$ equals 1 at its reference point (the
peak's height, before the amplitude scales it), not unit area. The integral
$\int \phi\,\mathrm{d}x$ is the area under that unit-height shape, which grows
with the peak's width (for a Gaussian it is $\sigma\sqrt{2\pi}$, not 1). So the
count is amplitude times area: a tall narrow peak and a short wide one can hold
the same number of counts.

GIGGLE counts only what is inside the range; it never extrapolates a peak past an
edge. If a peak's $\pm 3\sigma$ core runs off the edge, the reported number covers
only the in-range part and the Results panel warns you.

A fit works in whole bins, so the range snaps to bin edges. A range cutting
mid-bin would make "counts in range" ambiguous, so the data, the pivot, and the
area all use the snapped range.

## The shape catalogue

Each entry below gives the shape $\phi$, its integral $\int_a^b \phi\,\mathrm{d}x$
(which the count uses) when a closed form exists, and the FWHM where the shape
defines one. Throughout, every $\phi$ is 1 at its reference point; for peaks the
position is written $u = x - \mu$, and for backgrounds $p$ is the pivot, the
center of the fit range, $p = (x_{\min} + x_{\max})/2$.

### Peaks

**Gaussian**, with parameters `mean` ($\mu$) and `sigma` ($\sigma$):

$$ \phi(x) = \exp\!\left(-\frac{(x-\mu)^2}{2\sigma^2}\right) $$

$$ \int_a^b \phi\,\mathrm{d}x = \sigma\sqrt{\tfrac{\pi}{2}}
   \left[\operatorname{erf}\frac{b-\mu}{\sigma\sqrt2} - \operatorname{erf}\frac{a-\mu}{\sigma\sqrt2}\right],
   \qquad \mathrm{FWHM} = 2\sqrt{2\ln 2}\,\sigma \approx 2.3548\,\sigma . $$

**Lorentzian**, with parameters `mean` ($\mu$) and `gamma` ($\gamma$, the half
width at half maximum):

$$ \phi(x) = \frac{1}{1 + \big((x-\mu)/\gamma\big)^2}, \qquad
   \int_a^b \phi\,\mathrm{d}x = \gamma\left[\arctan\frac{b-\mu}{\gamma} - \arctan\frac{a-\mu}{\gamma}\right],
   \qquad \mathrm{FWHM} = 2\gamma . $$

**Voigt**, with parameters `mean` ($\mu$), `sigma` ($\sigma$, the Gaussian
width), and `gamma` ($\gamma$, the Lorentzian HWHM). A Voigt profile is a
Gaussian convolved
with a Lorentzian; it has no elementary closed form, so GIGGLE writes it through
the [Faddeeva function](https://en.wikipedia.org/wiki/Faddeeva_function) $w$ and
normalises by its central value:

$$ \phi(x) = \frac{\operatorname{Re} w\!\big((u + i\gamma)/(\sigma\sqrt2)\big)}
                  {\operatorname{Re} w\!\big(i\gamma/(\sigma\sqrt2)\big)} . $$

$w$ is evaluated with Humlicek's `w4` rational approximation (relative accuracy
$\sim 10^{-4}$, ample for line shapes; Humlicek, *JQSRT* **27**, 437, 1982). The
limits are handled exactly: as $\sigma \to 0$ the shape becomes a Lorentzian, as
$\gamma \to 0$ a Gaussian. The integral has no closed form and is computed by
adaptive Simpson integration (refined near the peak, so a narrow line in a wide range stays accurate). The FWHM uses the
Olivero–Longbothum approximation (accuracy $\sim 0.02\%$):

$$ \mathrm{FWHM} \approx 0.5346\,f_L + \sqrt{0.2166\,f_L^2 + f_G^2},
   \qquad f_G = 2\sqrt{2\ln 2}\,\sigma, \quad f_L = 2\gamma . $$

**Tailed Gaussian (gf3 / Hypermet)**, with parameters `mean` ($\mu$), `sigma`
($\sigma$), `tail_fraction` ($r \in [0,1]$), and `tail_length` ($\beta$). Many
detector peaks have a low-energy tail. GIGGLE uses the gf3 composite: a Gaussian
core blended with a single left-sided exponential tail, normalised to one at the
mean.

$$ \phi(u) = \frac{(1-r)\,e^{-u^2/2\sigma^2} + r\,T(u)}{N_0},
   \qquad N_0 = (1-r) + r\,\operatorname{erfc}(k), \quad k = \frac{\sigma}{\sqrt2\,\beta} $$

$$ T(u) = e^{\,u/\beta}\operatorname{erfc}\!\left(\frac{u}{\sigma\sqrt2} + k\right) . $$

Far above the mean the $e^{u/\beta}$ overflows while the $\operatorname{erfc}$
underflows; their product decays like a Gaussian, so for
$t = u/(\sigma\sqrt2) + k > 6$ that region is computed from the asymptotic form
$T(u) \approx e^{-k^2 - v^2}/(t\sqrt\pi)$ with $v = u/(\sigma\sqrt2)$, to stay
finite. The tail has a closed-form antiderivative (verified by differentiation),
so the integral is exact:

$$ \int T\,\mathrm{d}u = \beta\left[T(u) + e^{-k^2}\operatorname{erf}\frac{u}{\sigma\sqrt2}\right] . $$

Because the profile is asymmetric, its FWHM is found numerically by locating
both half-maximum crossings. Its reported **centroid is the core position**
$\mu$ (the gf3 convention), not the center of gravity of the skewed profile.

**Crystal Ball**, with parameters `mean` ($\mu$), `sigma` ($\sigma$), `alpha`
($\alpha$), and `n`. A Gaussian core with a power-law tail on one side, the
standard calorimeter or scintillator response. The sign of $\alpha$ sets the
side (low energy for $\alpha > 0$, high for $\alpha < 0$), as in ROOT. With
$t = (x-\mu)/\sigma$ (and $t \to -t$ when $\alpha < 0$),

$$ \phi(x) = \begin{cases}
   e^{-t^2/2}, & t > -\alpha,\\[4pt]
   A\,(B - t)^{-n}, & t \le -\alpha,
   \end{cases}
   \qquad A = \left(\frac{n}{|\alpha|}\right)^{\!n} e^{-\alpha^2/2},
   \quad B = \frac{n}{|\alpha|} - |\alpha|. $$

The core and tail meet smoothly at $t = -\alpha$ (matched value and first
derivative), and $\phi(\mu) = 1$. GIGGLE reproduces ROOT's
`ROOT::Math::crystalball_function` for either tail side, value for value. The
integral has no convenient closed form and is done by adaptive Simpson. The
FWHM is found numerically from the two half-maximum crossings: for small
$|\alpha|$ the tail can rise above half maximum, so using a Gaussian-core width would
be wrong.

**Landau**, with parameters `mean` ($\mu$) and `scale` ($s$). The Landau
energy-loss and straggling lineshape: a sharp rise with a long high-energy tail.
GIGGLE uses the CERNLIB `DENLAN` density $\mathcal{L}$, the rational
approximation of Kölbig and Schorr (*Comp. Phys. Commun.* **31**, 97, 1984) that
also backs ROOT's `TMath::Landau`. Those coefficients are a fixed published
recipe, not extracted from any particular ROOT version, so they do not change.
The density is shifted so its peak lands on $\mu$ and normalised to one there:

$$ \phi(x) = \frac{\mathcal{L}(\lambda)}{\mathcal{L}(\lambda_0)},
   \qquad \lambda = \frac{x - \mu}{s} + \lambda_0,
   \qquad \lambda_0 \approx -0.2228, \quad \mathcal{L}(\lambda_0) \approx 0.18066, $$

where $\lambda_0$ is the location of the Landau peak, so $\phi(\mu) = 1$. The
integral is by adaptive Simpson. The Landau mean diverges, so the reported
**centroid is the peak position** $\mu$ (the mode), and the FWHM is found
numerically from the two half-maximum crossings.

### Backgrounds

Backgrounds are anchored at the pivot $p$, so the amplitude is the background
*level* there.

| Shape | Parameters | $\phi(x)$ | $\int_a^b \phi\,\mathrm{d}x$ |
|---|---|---|---|
| constant | none | $1$ | $b-a$ |
| linear | `slope` $s$ | $1 + s(x-p)$ | $(b-a) + \tfrac{s}{2}\big[(b-p)^2-(a-p)^2\big]$ |
| quadratic | `slope` $s$, `curvature` $c$ | $1 + s(x-p) + c(x-p)^2$ | linear $+\ \tfrac{c}{3}\big[(b-p)^3-(a-p)^3\big]$ |
| exponential | `slope` $s$ | $e^{\,s(x-p)}$ | $\dfrac{e^{s(b-p)} - e^{s(a-p)}}{s}$ (→ $b-a$ as $s\to0$) |
| step | `edge`, `width` | $\tfrac12\operatorname{erfc}\!\dfrac{x-\mathrm{edge}}{\sqrt2\,\mathrm{width}}$ | closed form (see below) |

A Gaussian may also be used as a background (same formula as the Gaussian peak).
The **step** is one on the low-energy plateau and falls to zero across `width`,
modelling a Compton-edge-like shelf; note this is the documented exception to
"$\phi = 1$ at the pivot". Its antiderivative is

$$ \int \tfrac12\operatorname{erfc}(t)\,\mathrm{d}x
   = \frac{\mathrm{width}}{\sqrt2}\Big(t\operatorname{erfc}(t) - \tfrac{1}{\sqrt\pi}e^{-t^2}\Big),
   \qquad t = \frac{x-\mathrm{edge}}{\sqrt2\,\mathrm{width}} . $$

### Custom shapes

You can type any shape as a ROOT `TFormula` in $x$, with up to 16 free
parameters `[0]`, `[1]`, …. The formula is compiled and validated before use; it must not
contain an overall scale parameter, because that would be degenerate with the
amplitude ([`docs/usage.md`](usage.md) lists the full validation rules). Custom
shapes are evaluated through ROOT and integrated by adaptive Simpson, the same
as the Voigt.

## Where the uncertainties come from

Each count is a function of the fitted parameters $\theta = (A, \theta_1, \dots)$,
and its uncertainty comes straight from the fit, with no separate propagation
step that could add its own error. Here is where the formula comes from.

The fit returns not only the best-fit parameters but the **covariance matrix**
$C$. Its diagonal entries are the parameter variances, $C_{ii} = \sigma_i^2$, and
the off-diagonal entries $C_{ij}$ record how two parameters move together (their
correlation). Now suppose the true parameters sit a little off the best fit,
$\theta = \hat\theta + \Delta\theta$. The count $N(\theta)$ shifts with them; for
small wiggles, keep only the first (linear) term of its Taylor expansion:

$$ \Delta N \approx \sum_i g_i\,\Delta\theta_i,
   \qquad g_i = \frac{\partial N}{\partial \theta_i}, $$

so $g_i$ is just the slope of $N$ in parameter $i$. The uncertainty $\sigma_N^2$
is the **variance** of $N$: the average of $\Delta N^2$ over the way the fitted
parameters would scatter if the measurement were repeated many times. Multiplying the
sum out,

$$ \sigma_N^2 = \langle \Delta N^2\rangle
   = \Big\langle \Big(\sum_i g_i\,\Delta\theta_i\Big)\Big(\sum_j g_j\,\Delta\theta_j\Big) \Big\rangle
   = \sum_{i,j} g_i\,g_j\,\langle \Delta\theta_i\,\Delta\theta_j\rangle . $$

The averaged product $\langle \Delta\theta_i\,\Delta\theta_j\rangle$ is exactly the
covariance $C_{ij}$ (that is what the matrix holds), so

$$ \sigma_N^2 = \sum_{i,j} g_i\,C_{ij}\,g_j = g^{\mathsf T} C\, g. $$

With a single parameter and nothing to correlate, this is the familiar
$\sigma_N = |\partial N/\partial\theta|\,\sigma_\theta$; the matrix form just sums
every parameter's slope and every pair's correlation at once. For a count the
gradient is

$$ g = \Big[\ \underbrace{\textstyle\int \phi\,\mathrm{d}x}_{\partial N/\partial A},\ \
   \underbrace{A\,\partial_{\theta_j}\!\textstyle\int \phi\,\mathrm{d}x}_{\partial N/\partial \theta_j}\ \Big] . $$

The amplitude derivative is the shape integral itself; the shape-parameter
derivatives are central finite differences on that integral (step
$\max(|\theta_j|\cdot 10^{-6},\ 10^{-9})$). The **total** counts use the same
quadratic form across *all* parameters at once, so correlations between
components are included, so the reported total error is not a naive quadrature
sum.

### Centroid and FWHM

For shapes that define them, GIGGLE reports a centroid (the fitted mean) and a
full width at half maximum, with errors propagated from the parameter errors
through the closed forms above (the asymmetric shapes, tailed Gaussian, Crystal
Ball, and Landau, use a numeric FWHM from the two half-maximum crossings). For the Voigt and the numeric FWHM, the propagation currently uses the
per-parameter errors and neglects the σ–γ correlation; the **count** uncertainty
above always uses the full covariance block.

## Goodness of fit: which statistic

The fit runs through ROOT's `ROOT::Fit::Fitter` with the Minuit2 / Migrad
minimiser. You choose the statistic, FitPanel-style:

- **Chi-square (default).** Minimises $\sum_\text{bins}(y_i - f_i)^2/\sigma_i^2$
  and **skips empty bins** (they have no defined error). Best when bins are well
  populated, and it keeps the data-versus-model-total comparison honest. The
  per-bin error falls back to $\sqrt{y_i}$ when the histogram carries no stored
  errors.
- **Poisson likelihood.** Maximises the Poisson likelihood and **includes empty
  bins**. Use it for low-count spectra, where chi-square underestimates yields.
  By construction it reproduces the data total.

A reduced $\chi^2/\mathrm{ndf}$ near one indicates a model that describes the
data without over-fitting. The degrees of freedom are the number of fitted bins
minus the number of free parameters; chi-square skips empty bins so they do not
count, while Poisson likelihood includes them, and fixing a parameter raises the
ndf by one.

## The independent cross-check

After every converged fit, GIGGLE re-fits the same data a second,
differently parametrised way, ROOT's `TF1NormSum`, in which each component's
in-range counts are *themselves* the fit coefficients (up to the constant
bin-width factor), and compares the counts and uncertainties from both routes.
The cross-check exists because the count's error is obtained by propagating the
fit covariance through the integral, which is a first-order (linear)
approximation: it is exact only when the count changes in proportion to each
parameter. When a parameter enters nonlinearly (the count curves with it instead
of following the straight line the Taylor step above assumed, for example because
a width appears squared or inside an exponent), the propagated error can be a
little off, and the true error can even be lopsided. `TF1NormSum` makes each
count a fit parameter directly, with no propagation step, so agreement between the
two routes confirms the propagated values. When asymmetric (MINOS) errors are requested, the
asymmetric count errors are read from this re-fit's profiled coefficients, so
they are reported only when the cross-check runs.

A component **agrees** when

$$ |N_\text{ours} - N_\text{ref}| \le \max\big(0.01\,s,\ 0.3\,e\big)
   \quad\text{and}\quad
   |\sigma_\text{ours} - \sigma_\text{ref}| \le 0.10\,e, $$

where $s = \max(|N_\text{ours}|, |N_\text{ref}|)$ and
$e = \max(\sigma_\text{ours}, \sigma_\text{ref})$. The Results panel shows the
largest count deviation as a percentage. "Counts independently verified" means
two different algorithms produced the same numbers; if they disagree, the fit is
unstable and the panel says so in red.

The cross-check is **skipped** (and labelled as not performed) for histograms
with variable bin widths, or when a component's height is fixed, because a fixed
height cannot be mirrored as a free `TF1NormSum` coefficient.

### Why not just fit with `TF1NormSum`?

If `TF1NormSum` reports the counts directly, why not make it the fit itself and
skip the propagation? Two reasons.

It cannot cover every case. Its coefficient equals the in-range count only up to
a constant bin-width factor, so it needs uniform bins, and a fixed (held) count
cannot be a free coefficient. Those two cases are what the cross-check reports as
"not performed." GIGGLE's own engine fits in the amplitude
parametrisation the panel uses, and additionally handles variable bins and fixed
counts, while bounded parameters and custom formulas run through both routes.

More importantly, a check is only worth something if it is independent. If
`TF1NormSum` were the only method, there would be nothing to compare it against.
Running it as a second, differently parametrised fit supplies that comparison, and
it is independent in the part that matters: the two routes share the shape functions,
the data, and the minimiser, but they obtain each count and its error in genuinely different
ways. One reads the count straight off a fit coefficient, the other forms it as
amplitude times integral and propagates the error through the covariance. So, when the
two agree, the count conversion and its propagated uncertainty, which is the error-prone
step, are confirmed. With that said, a bug inside a shape function itself would not be
caught, since both routes would share it.
