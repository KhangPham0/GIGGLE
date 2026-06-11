#ifndef GIGGLE_CORE_SPECTRUM_SOURCE_H
#define GIGGLE_CORE_SPECTRUM_SOURCE_H

#include <string>
#include <vector>

#include "HistogramData.h"

namespace giggle {

// One histogram available in a source. Listing is kept cheap (nothing is
// read from disk beyond the file's key table), so a file with thousands of
// histograms can be browsed instantly; contents are read by Load.
struct HistogramInfo
{
    std::string path;  // location within the source, e.g. "spectra/h_ex"
    std::string title; // display title
};

// Read access to a collection of 1D histograms (in practice: a ROOT file).
// Implemented in rootbridge; everything else in GIGGLE only sees this
// interface.
class SpectrumSource
{
public:
    virtual ~SpectrumSource() = default;

    // All 1D histograms in the source, in file order. Paths use '/' for
    // subdirectories.
    virtual std::vector<HistogramInfo> List() = 0;

    // Reads one histogram by its path. Throws std::runtime_error when the
    // path does not exist or is not a 1D histogram.
    virtual HistogramData Load(const std::string& path) = 0;
};

} // namespace giggle

#endif
