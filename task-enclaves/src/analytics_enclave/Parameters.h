/*
* Copyright 2021 European Union
*
* Licensed under the EUPL, Version 1.2 or – as soon they will be approved by 
* the European Commission - subsequent versions of the EUPL (the "Licence");
* You may not use this work except in compliance with the Licence.
* You may obtain a copy of the Licence at:
*
* https://joinup.ec.europa.eu/software/page/eupl
*
* Unless required by applicable law or agreed to in writing, software 
* distributed under the Licence is distributed on an "AS IS" basis,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the Licence for the specific language governing permissions and 
* limitations under the Licence.
*/ 

#pragma once

#include "../pseudonymisation_key_enclave/Entities.h"
#include <cstddef>
#include <cstdint>

namespace eurostat {
namespace enclave {

// Data analysis algorithm parameters
// ψ in the document
constexpr double day_quantisation_threshold = 10;
// φ in the document
constexpr double sub_period_quantisation_threshold = 0.5;
// ξ in the document
constexpr double sdc_threshold = 1;

// Indicator parameters
// `k` for the SDC K-anonymity used in the histogram value formatting.
// Histogram bin values `< k` are displayed as the following replacement string.
#if !defined(NDEBUG) || defined(EDEBUG)
// For development builds (Debug, Prereelease) use a value that actually does not
// apply any anonymity.
constexpr std::uint64_t indicators_k_anonymity = 1;
#else
// For release builds, use the value specified in the architecture document.
constexpr std::uint64_t indicators_k_anonymity = 20;
#endif
constexpr char const * const indicators_k_anonymity_replacement = "NA";

constexpr std::size_t aes_block_size = 16;
constexpr std::size_t sha256_size = 32;
constexpr std::size_t hash_bytes = 12;
constexpr std::size_t hmac_bytes = 4;
static_assert(hash_bytes + hmac_bytes == aes_block_size, "Needs to be as big as one AES block.");
static_assert(PseudonymisationKeyLength == aes_block_size, "Needs to be as big as one AES block.");


using topic_name_t = char const *;
using argument_name_t = char const *;

namespace input_names {
/** Contains exactly one `ReportRequest`. Uploaded by NSI. */
constexpr topic_name_t nsi_input = "nsi_input";
/** Contains exactly one `ReportRequest`. Uploaded by NSI. */
constexpr topic_name_t periodic_pseudonymisation_key = "periodic_pseudonymisation_key";
}

namespace output_names {
/** Contains `TopAnchorDistributionReport`. */
constexpr topic_name_t top_anchor_distribution_report = "top_anchor_distribution_report";
/** Contains `FingerprintReport`. */
constexpr topic_name_t fingerprint_report = "fingerprint_report";
/** Contains `FunctionalUrbanFingerprintReport`. */
constexpr topic_name_t functional_urban_fingerprint_report = "functional_urban_fingerprint_report";
/** Contains `Statistics`. */
constexpr topic_name_t statistics = "statistics";
/** Contains `Statistics`. */
constexpr topic_name_t application_log = "application_log";
}

namespace arguments {
/** No matter the value, if it is present when we wait for
 * `UserFootprintUpdates` files, we reset the state instead and wait for a new
 * NSI request. */
constexpr argument_name_t cancel = "cancel";
/** No matter the value, if it is present when we wait for
 * `UserFootprintUpdates` files, we finish the report. */
constexpr argument_name_t finish_report = "finish-report";
/** The `UserFootprintUpdates` file to load directly from the file system,
 * circumventing the usual topics for input data to sidestep the data
 * encryption and uploading cost for these huge files, which is both not
 * necessary: The files are created by the host, so no confidentiality is lost.
 * */
constexpr argument_name_t file = "file";
/** The task runner informs us which period we are working with, not. This is
 * more like a sanity check. If it matches the max period from the NSI request,
 * it will perform the report calculations. */
constexpr argument_name_t period = "period";
}

} // namespace enclave
} // namespace eurostat
