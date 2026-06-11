#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <string>

#include "TFile.h"
#include "TH1D.h"
#include "TH2D.h"

#include "rootbridge/RootFileSource.h"

using namespace giggle;

// Writes a small ROOT file with known contents and returns its path:
//
//   h_simple            uniform bins, no stored errors
//   h_weighted          uniform bins, explicit (Sumw2) errors
//   h_var               variable-width bins
//   h_2d                a TH2D, which List and Load must reject
//   spectra/h_nested    inside a subdirectory
static std::string MakeTestFile()
{
    static std::string path;
    if (!path.empty())
    {
        return path; // already created during this test run
    }
    path = (std::filesystem::temp_directory_path() / "giggle_rootbridge_test.root").string();

    // Keep histograms independent of ROOT's directory ownership, so the
    // stack-allocated objects below are not deleted by the file.
    TH1::AddDirectory(false);

    TFile file(path.c_str(), "RECREATE");

    TH1D simple("h_simple", "A simple spectrum", 5, 0.0, 10.0);
    simple.SetBinContent(1, 4.0);
    simple.SetBinContent(2, 9.0);
    simple.SetBinContent(3, 16.0);
    simple.SetBinContent(4, 9.0);
    simple.SetBinContent(5, 4.0);
    simple.Write();

    TH1D weighted("h_weighted", "With explicit errors", 3, 0.0, 3.0);
    weighted.Sumw2();
    weighted.SetBinContent(1, 10.0);
    weighted.SetBinError(1, 5.0);
    weighted.SetBinContent(2, 20.0);
    weighted.SetBinError(2, 6.0);
    weighted.SetBinContent(3, 30.0);
    weighted.SetBinError(3, 7.0);
    weighted.Write();

    double edges[4] = { 0.0, 1.0, 3.0, 6.0 };
    TH1D variable("h_var", "Variable bin widths", 3, edges);
    variable.SetBinContent(1, 1.0);
    variable.SetBinContent(2, 2.0);
    variable.SetBinContent(3, 3.0);
    variable.Write();

    TH2D twoD("h_2d", "A 2D histogram", 4, 0.0, 4.0, 4, 0.0, 4.0);
    twoD.Write();

    file.mkdir("spectra")->cd();
    TH1D nested("h_nested", "Nested spectrum", 2, 0.0, 2.0);
    nested.SetBinContent(1, 7.0);
    nested.SetBinContent(2, 8.0);
    nested.Write();

    file.Close();
    return path;
}

TEST_CASE("opening a missing file throws")
{
    CHECK_THROWS(RootFileSource("/nonexistent/no_such_file.root"));
}

TEST_CASE("List finds every 1D histogram and nothing else")
{
    RootFileSource source(MakeTestFile());
    std::vector<HistogramInfo> list = source.List();

    REQUIRE(list.size() == 4);
    CHECK(list[0].path == "h_simple");
    CHECK(list[0].title == "A simple spectrum");
    CHECK(list[1].path == "h_weighted");
    CHECK(list[2].path == "h_var");
    CHECK(list[3].path == "spectra/h_nested");

    for (const HistogramInfo& info : list)
    {
        CHECK(info.path != "h_2d");
    }
}

TEST_CASE("Load reads contents, edges, and title")
{
    RootFileSource source(MakeTestFile());
    HistogramData data = source.Load("h_simple");

    CHECK(data.IsValid());
    CHECK(data.name == "h_simple");
    CHECK(data.title == "A simple spectrum");
    CHECK(data.BinCount() == 5);
    CHECK(data.XMin() == 0.0);
    CHECK(data.XMax() == 10.0);
    CHECK(data.binEdges[1] == doctest::Approx(2.0));
    CHECK(data.counts == std::vector<double>{ 4.0, 9.0, 16.0, 9.0, 4.0 });

    // No Sumw2: errors stay empty and fall back to sqrt(counts).
    CHECK(data.errors.empty());
    CHECK(data.BinError(2) == doctest::Approx(4.0));
}

TEST_CASE("Load keeps explicit errors when the histogram has them")
{
    RootFileSource source(MakeTestFile());
    HistogramData data = source.Load("h_weighted");

    CHECK(data.IsValid());
    REQUIRE(data.errors.size() == 3);
    CHECK(data.errors[0] == doctest::Approx(5.0));
    CHECK(data.BinError(2) == doctest::Approx(7.0));
}

TEST_CASE("Load handles variable bin widths")
{
    RootFileSource source(MakeTestFile());
    HistogramData data = source.Load("h_var");

    CHECK(data.IsValid());
    CHECK(data.binEdges == std::vector<double>{ 0.0, 1.0, 3.0, 6.0 });
    CHECK(data.counts == std::vector<double>{ 1.0, 2.0, 3.0 });
}

TEST_CASE("Load reaches histograms inside subdirectories")
{
    RootFileSource source(MakeTestFile());
    HistogramData data = source.Load("spectra/h_nested");

    CHECK(data.IsValid());
    CHECK(data.counts == std::vector<double>{ 7.0, 8.0 });
}

TEST_CASE("Load rejects missing paths and non-1D histograms")
{
    RootFileSource source(MakeTestFile());
    CHECK_THROWS(source.Load("no_such_histogram"));
    CHECK_THROWS(source.Load("h_2d"));
}
