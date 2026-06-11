#ifndef GIGGLE_ROOTBRIDGE_ROOT_FILE_SOURCE_H
#define GIGGLE_ROOTBRIDGE_ROOT_FILE_SOURCE_H

#include <memory>
#include <string>
#include <vector>

#include "core/SpectrumSource.h"

class TFile;

namespace giggle {

// Reads histograms from a ROOT file.
class RootFileSource : public SpectrumSource
{
public:
    // Opens the file read-only; throws std::runtime_error when the file
    // cannot be opened.
    explicit RootFileSource(const std::string& filePath);
    ~RootFileSource() override;

    std::vector<HistogramInfo> List() override;
    HistogramData Load(const std::string& path) override;

    const std::string& FilePath() const { return m_filePath; }

private:
    std::string m_filePath;
    std::unique_ptr<TFile> m_file;
};

} // namespace giggle

#endif
