#ifndef GIGGLE_CORE_HISTOGRAM_DATA_H
#define GIGGLE_CORE_HISTOGRAM_DATA_H

#include <cmath>
#include <string>
#include <vector>

namespace giggle {

// A 1D histogram as plain data, independent of where it came from.
struct HistogramData
{
    std::string name;  // path inside the source file, e.g. "spectra/h_ex"
    std::string title; // display title

    std::vector<double> binEdges; // ascending; size = bin count + 1
    std::vector<double> counts;   // size = bin count
    std::vector<double> errors;   // per-bin errors; empty means sqrt(counts)

    int BinCount() const
    {
        return static_cast<int>(counts.size());
    }

    double XMin() const
    {
        return binEdges.empty() ? 0.0 : binEdges.front();
    }

    double XMax() const
    {
        return binEdges.empty() ? 0.0 : binEdges.back();
    }

    // The error on one bin, falling back to sqrt(counts) when no explicit
    // errors are present.
    double BinError(int bin) const
    {
        if (!errors.empty())
        {
            return errors[bin];
        }
        return std::sqrt(counts[bin] > 0.0 ? counts[bin] : 0.0);
    }

    // True when the edge/count/error sizes are consistent and the edges are
    // strictly ascending.
    bool IsValid() const
    {
        if (binEdges.size() != counts.size() + 1)
        {
            return false;
        }
        if (!errors.empty() && errors.size() != counts.size())
        {
            return false;
        }
        for (size_t i = 1; i < binEdges.size(); ++i)
        {
            if (binEdges[i] <= binEdges[i - 1])
            {
                return false;
            }
        }
        return true;
    }
};

} // namespace giggle

#endif
