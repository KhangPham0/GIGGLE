#include "RootFileSource.h"

#include <stdexcept>
#include <unordered_set>

#include "TClass.h"
#include "TDirectory.h"
#include "TFile.h"
#include "TH1.h"
#include "TKey.h"

namespace giggle {

namespace {

// True when the key holds a 1D histogram (TH2/TH3 also inherit TH1, so they
// must be excluded explicitly).
bool Is1DHistogramKey(const TKey& key)
{
    TClass* keyClass = TClass::GetClass(key.GetClassName());
    if (keyClass == nullptr || !keyClass->InheritsFrom("TH1"))
    {
        return false;
    }
    return !keyClass->InheritsFrom("TH2") && !keyClass->InheritsFrom("TH3");
}

bool IsDirectoryKey(const TKey& key)
{
    TClass* keyClass = TClass::GetClass(key.GetClassName());
    return keyClass != nullptr && keyClass->InheritsFrom("TDirectory");
}

// Walks a directory recursively and appends every 1D histogram it finds.
// Only the file's key table is touched; histogram contents stay on disk.
void ListDirectory(TDirectory& directory, const std::string& pathPrefix,
                   std::vector<HistogramInfo>& result)
{
    // A key appears once per stored cycle; only report each name once.
    std::unordered_set<std::string> seen;

    for (TObject* object : *directory.GetListOfKeys())
    {
        TKey* key = static_cast<TKey*>(object);
        if (!seen.insert(key->GetName()).second)
        {
            continue;
        }

        if (Is1DHistogramKey(*key))
        {
            HistogramInfo info;
            info.path = pathPrefix + key->GetName();
            info.title = key->GetTitle();
            result.push_back(info);
        }
        else if (IsDirectoryKey(*key))
        {
            auto* subdirectory = directory.Get<TDirectory>(key->GetName());
            if (subdirectory != nullptr)
            {
                ListDirectory(*subdirectory, pathPrefix + key->GetName() + "/", result);
            }
        }
    }
}

} // namespace

RootFileSource::RootFileSource(const std::string& filePath) : m_filePath(filePath)
{
    m_file.reset(TFile::Open(filePath.c_str(), "READ"));
    if (m_file == nullptr || m_file->IsZombie())
    {
        m_file.reset();
        throw std::runtime_error("could not open ROOT file: " + filePath);
    }
}

RootFileSource::~RootFileSource() = default;

std::vector<HistogramInfo> RootFileSource::List()
{
    std::vector<HistogramInfo> result;
    ListDirectory(*m_file, "", result);
    return result;
}

HistogramData RootFileSource::Load(const std::string& path)
{
    std::unique_ptr<TH1> histogram(m_file->Get<TH1>(path.c_str()));
    if (histogram == nullptr)
    {
        throw std::runtime_error("no histogram \"" + path + "\" in " + m_filePath);
    }
    if (histogram->GetDimension() != 1)
    {
        throw std::runtime_error("\"" + path + "\" is not a 1D histogram");
    }
    // Detach from the file so the unique_ptr is the sole owner.
    histogram->SetDirectory(nullptr);

    int binCount = histogram->GetNbinsX();

    HistogramData data;
    data.name = path;
    data.title = histogram->GetTitle();

    data.binEdges.reserve(binCount + 1);
    for (int bin = 1; bin <= binCount; ++bin)
    {
        data.binEdges.push_back(histogram->GetXaxis()->GetBinLowEdge(bin));
    }
    data.binEdges.push_back(histogram->GetXaxis()->GetBinUpEdge(binCount));

    data.counts.reserve(binCount);
    for (int bin = 1; bin <= binCount; ++bin)
    {
        data.counts.push_back(histogram->GetBinContent(bin));
    }

    // Explicit errors are only kept when the histogram tracks them
    // (Sumw2); otherwise HistogramData falls back to sqrt(counts).
    if (histogram->GetSumw2N() > 0)
    {
        data.errors.reserve(binCount);
        for (int bin = 1; bin <= binCount; ++bin)
        {
            data.errors.push_back(histogram->GetBinError(bin));
        }
    }

    return data;
}

} // namespace giggle
