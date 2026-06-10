#ifndef GIGGLE_CORE_SERIALIZATION_H
#define GIGGLE_CORE_SERIALIZATION_H

#include <string>

#include "nlohmann/json.hpp"

#include "FitModel.h"
#include "FitResult.h"

namespace giggle {

// ordered_json keeps keys in insertion order, so the files stay readable.
using Json = nlohmann::ordered_json;

// Bumped whenever the layout of serialized documents changes, so scripts
// reading GIGGLE files can detect what they are looking at.
inline constexpr int kSchemaVersion = 1;

// Where a result came from: enough to trace any number back to its origin.
struct Provenance
{
    std::string giggleVersion;
    std::string timestamp;     // UTC, ISO 8601
    std::string sourceFile;    // path of the file the histogram came from
    std::string histogramName; // histogram within that file
};

// Fills in the current version and timestamp.
Provenance MakeProvenance(const std::string& sourceFile, const std::string& histogramName);

// FitModel <-> JSON. FitModelFromJson throws an std::exception subclass when
// the document is malformed or fields are missing.
Json ToJson(const FitModel& model);
FitModel FitModelFromJson(const Json& json);

// The complete results document written after a fit: schema version,
// provenance, the model that was fitted, and the results. The model is
// needed to give the result parameters their names.
Json MakeResultsDocument(const Provenance& provenance, const FitModel& model, const FitResult& result);

} // namespace giggle

#endif
