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

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <sgx_key.h>
#include <sgx_trts.h>
#include <sharemind-hi/common/Messages.h>
#include <sharemind-hi/enclave/common/EnclaveException.h>
#include <sharemind-hi/enclave/common/EncryptedData.h>
#include <sharemind-hi/enclave/common/File.h>
#include <sharemind-hi/enclave/common/Log.h>
#include <sharemind-hi/enclave/common/SafeOcall.h>
#include <sharemind-hi/enclave/task/Messages.h>
#include <sharemind-hi/enclave/task/Task.h>
#include <sharemind-hi/enclave/task/stream/Streams.h>
#include <sharemind-hi/enclave/task/stream/Streams_detail.h>
#include <sharemind-hi/enclave/task/stream/TaskDataStream.h>
#include <sharemind-hi/filesystem/FileOpenMode.h>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Entities.h"
#include "FullAnalysis.h"
#include "HiInternalApiDuplication.h"
#include "Parameters.h"
#include "Seal.h"
#include "SgxEncryptedFile.h"

#define PRANGE(ptr, len) (ptr), (ptr) + (len)

namespace eurostat {
namespace enclave {
namespace {

using namespace sharemind_hi::enclave;
using namespace sharemind_hi;

/** Help outside requests filter invalid invocations of the enclave from instances that failed due to some internal problem. Therefore, prepend an easily machine readable prefix to the error message. */
class InvalidRequest : EnclaveException {
public:
    InvalidRequest(std::string const & what)
        : EnclaveException(":AE01: " + what)
    {}
};

/**
   The path were we store the persistent state of this enclave between
   `taskRun` invocations. It is stored at the `data_path` as configured in the
   `server.yaml` configuration file.
   It is set in the `init()` function.
 */
std::string persistent_path;
/** Where to store the state file. Set in the `init()` function. */
std::string state_file_path;

void init() {
    static constexpr std::size_t const maxPathSize = 256u;
    persistent_path.resize(maxPathSize);
    uint8_t fs_id[16] = {};
    ocallOrThrow(get_data_file_path_ocall,"Failed to retrieve data file path", &fs_id, &persistent_path[0u], maxPathSize);
    persistent_path.resize(std::strlen(persistent_path.c_str()));
    // At the end of the path is now the suffix "/000...000/data" which needs to be
    // removed to get the persistent path.
    auto pos = persistent_path.find_last_of('/');
    if (pos == persistent_path.npos || pos == 0) {
        throw EnclaveException("The persistent path could not be deduced from <" + persistent_path + ">");
    }
    pos = persistent_path.find_last_of('/',pos - 1);
    if (pos == persistent_path.npos) {
        throw EnclaveException("The persistent path could not be deduced from <" + persistent_path + ">");
    }
    persistent_path.erase(pos + 1);
    state_file_path = persistent_path + "state_file";
    enclave_printf_log("persistent path: %s, state file path: %s",
                       persistent_path.c_str(),
                       state_file_path.c_str());
}

std::string s_file_path(bool const index) {
    return persistent_path + "s_file" + (index ? "1" : "0");
};

std::list<EnclaveDataInfo> const & find_topic(TaskInputs const & inputs,
                                              char const * const name)
{
    auto const & topics = inputs.inputs();
    for (auto const & topic : topics) {
        if (topic.name.toString() == name) { return topic.data; }
    }
    throw EnclaveException(std::string{"Input <"} + name + "> not found");
}

struct ReportRequest {
    Period first_period;
    Period last_period;

    // Using a std::uint64_t to prevent padding.
    std::uint64_t with_calibration;

    // Reference areas information. Upon each H-file-processing request it
    // will be transformed back into the `ReferenceAreas` structure.
    // This cannot be dynamically sized, as we use plain C-style serialization.
    std::size_t num_of_reference_areas;
    std::array<ReferenceArea, ReferenceArea::MAX_ELEMENTS_PER_NSI_REPORT_REQUEST> reference_areas;

    // Census residents information. Upon each H-file-processing request it
    // will be transformed back into the `CensusResidents` structure.
    // This cannot be dynamically sized, as we use plain C-style serialization.
    std::size_t num_of_census_residents;
    std::array<CensusResident, CensusResident::MAX_ELEMENTS_PER_NSI_REPORT_REQUEST> census_residents;
} /* Don't use `packed`, makes object usage needlessly complicated. Instead,
     insert padding manually if required. */;
static_assert(sizeof(ReportRequest) == 17000032, "");
static_assert(sizeof(ReportRequest)
                      == 0 // Aid for automatic formatting
                                 + sizeof(ReportRequest::first_period)
                                 + sizeof(ReportRequest::last_period)
                                 + sizeof(ReportRequest::with_calibration)
                                 + sizeof(ReportRequest::num_of_reference_areas)
                                 + sizeof(ReportRequest::reference_areas)
                                 + sizeof(ReportRequest::num_of_census_residents)
                                 + sizeof(ReportRequest::census_residents)
              // Aid for automatic formatting
              ,
              "`ReportRequest` contains implicit padding. Replace it with "
              "explicit padding. Otherwise it becomes hard to construct this "
              "message in another language that is not C++.");

/**
  This state is persistent, read in the start and written at the end of a
  successfull run.
*/
struct State {
    // It's a simple state machine
    enum STATE_MACHINE {
        AWAITING_NEW_NSI_REPORT_REQUESTS,
        AWAITING_NEW_H_FILES,
    };

    struct AwaitingNewRequests {
        // Should be empty.
    };
    struct AwaitingNewHFiles {
        ReportRequest report_request;
        Period next_expected_period;
    };

    STATE_MACHINE state = AWAITING_NEW_NSI_REPORT_REQUESTS;
    union {
        AwaitingNewRequests awaiting_new_requests;
        AwaitingNewHFiles awaiting_new_h_files;
    };

    /**
     * This variable is used to keep track of whether a new NSI input (report
     * request) arrived.
     * Use the topic size instead of the last data id, as it can be initially
     * `0` (the first data id is `0` itself), and it is easier in the comparison
     * code.
     */
    std::size_t last_seen_nsi_inputs_topic_size = 0;

    /**
     * The crypto key to use with the `sgx_fopen` API. The `sgx_fopen_auto_key`
     * API is not used, as then S files from older report requests could be
     * "imported" into this report request. This is changed on each update.
     */
    SgxFileKey s_file_key = {};

    /**
     * Valid values: 0, 1.
     * We read from the S file and write to the S file in the same pipe
     * command. Hence, we need to use different S files. In this case, using a
     * double buffer, switching back and forth. */
    bool s_file_name_index = 0;

    void go_into_request_await_state() noexcept
    {
        state = State::AWAITING_NEW_NSI_REPORT_REQUESTS;
        awaiting_new_requests = {};
        s_file_name_index = 0;
    }

    void go_into_h_processing_state(ReportRequest const & report_request) noexcept
    {
        state = State::AWAITING_NEW_H_FILES;
        awaiting_new_h_files.next_expected_period = report_request.first_period;
        awaiting_new_h_files.report_request = report_request;
        s_file_name_index = 0;
    }
};
// Make sure that we can really memcpy it into a file, and back from the file
// into the object.
static_assert(std::is_trivially_copyable<State>::value, "");

std::unique_ptr<State> load_state();
void store_state(State const &);
/**
   `process_state` matches the state against the parameters and calls one of
   the other `process_*` functions.
  */
State & process_state(State &,
                      TaskInputs const &,
                      TaskOutputs &,
                      std::vector<std::string> & old_s_files_to_delete,
                      Log &);
State & process_nsi_report_request_digestion(State &, TaskInputs const &, Log &);
State & process_h_file(State &,
                       TaskInputs const &,
                       TaskOutputs &,
                       std::vector<std::string> & old_s_files_to_delete,
                       Log &,
                       std::string const & h_file,
                       Period const period);
State & process_cancel(State &,
                       std::vector<std::string> & old_s_files_to_delete,
                       Log & application_log);
State &
process_manually_finish_report(State &,
                               TaskOutputs &,
                               std::vector<std::string> & old_s_files_to_delete,
                               Log &);

} // namespace
} // namespace enclave
} // namespace eurostat

namespace sharemind_hi {
namespace enclave {
void run(TaskInputs const & inputs, TaskOutputs & outputs) {
    using namespace eurostat::enclave;
    enclave_printf_log("Running analytics enclave");
    Log application_log;

    init();

    // Note: How to prevent that this enclave is not run twice in parallel?
    // The server can specify that the task enclave thread pool only has one
    // runner, but this is not guaranteed (as it is not part of the DFC).

    std::vector<std::string> old_s_files_to_delete;

    store_state(process_state(*load_state().get(),
                              inputs,
                              outputs,
                              old_s_files_to_delete,
                              application_log));

    // The state has been overwritten, so the old S files can be delete, too.
    for (auto const & old_s_file_to_delete : old_s_files_to_delete) {
        try {
            SgxEncryptedFile::remove(old_s_file_to_delete);
        } catch (...) {
            /* ignore */
        }
    }

    outputs.put(output_names::application_log,
                application_log.c_str(),
                application_log.size());
}
} // namespace enclave
} // namespace sharemind_hi

namespace eurostat {
namespace enclave {
namespace {

CensusResidents deserialize(CensusResident const * cur,
                            CensusResident const * const end)
{
    CensusResidents result;
    for (; cur != end; ++cur) {
        result.emplace(std::make_pair(cur->index, cur->value));
    }
    return result;
}

ReferenceAreas deserialize(ReferenceArea const * cur,
                           ReferenceArea const * const end)
{
    ReferenceAreas result;

    // Note: This verification is also performed in the NSI request selection
    // function. During regular analysis we thus can be sure that it won't
    // fail.
    for (; cur != end; ++cur) {
        // The first condition verifies that the indices start from 0 and are
        // incrementing without gaps.
        if (cur->id > result.size() || result.size() - cur->id > 1) {
            throw EnclaveException("The reference area indices are invalid.");
        } else if (cur->id == result.size()) {
            result.push_back({cur->tile_index});
        } else {
            assert(cur->id + 1u == result.size()); // ensured by the top condition.
            result.back().emplace(cur->tile_index);
        }
    }
    return result;
}

void read_h_metadata_file(std::string const & h_file_path, Log & application_log) {
    // The string buffer.
    std::string metadata;

    // Read the file content into the string buffer.
    File metadata_file{h_file_path + ".meta", FileOpenMode::FILE_OPEN_READ_ONLY};
    // XXX `size` could be rather big, but this just leads to an OOM, no problem.
    auto const size = metadata_file.size();
    metadata.resize(size);
    metadata_file.read(&metadata[0], size);

    application_log.append("\nH metadata:\n");
    application_log.append(metadata);
    application_log.append("\n");
}

// Have a single function so it is consistent:
void log_request_arguments(ReportRequest const & report_request,
                           Log & application_log)
{
    application_log.append("With calibration: ");
    application_log.append(report_request.with_calibration ? "true\n" : "false\n");
    application_log.append("First period: ");
    application_log.append(std::to_string(report_request.first_period));
    application_log.append(", last period: ");
    application_log.append(std::to_string(report_request.last_period));
    application_log.append("\n");
}

void log_skipped_periods(uint64_t const first_skipped_inclusive,
                         uint64_t const last_skipped_exclusive,
                         Log & application_log)
{
    // Log any skipped periods (6.2.1).
    for (uint64_t skipped = first_skipped_inclusive;
         skipped < last_skipped_exclusive;
         ++skipped) {
        application_log.append("Skipped period ");
        application_log.append(std::to_string(skipped));
        application_log.append("\n");
    }
}

/**
  State handling:
    Three states:
      Committed: A good state.
      Dirty: A partial / inconsistent state. Created during run time.
      Committing: A good state. When a dirty state shall be saved (since it currently is in a good state), it is moved into a special directory. If the server crashes now, during restart it can recover from the committing state.

    Actually, in the TE the Dirty automatically becomes "Committing" when the enclave finishes.

    Wellp, to get something started, let's just ignore server crashes and write errors.
    We always commit the state file to the same file in the end. The S file will
    be written to the toggled place. The S file is only updated once per task run.

 */


// Only a single thing is sealed. Hence a single AAD is sufficient.
char const * const sealing_aad = "analysis_enclave_state_file";

/** Return the state as a `std::unique_ptr`, as it is actually rather large. */
std::unique_ptr<State> load_state()
{
    // Value-initialize (zero initialized).
    auto result = std::unique_ptr<State>{new State{}};

    // We assume that file loading only fails if it does not exist yet. In
    // that case the result is the zeroed state.
    try {
        (void)File(state_file_path, FileOpenMode::FILE_OPEN_READ_ONLY);
    } catch (...) {
        enclave_printf_log(
                "Loading the state file failed. Using a new state instead.");
        return result;
    }

    // Open it a second time, now it should not fail (ignoring TOCTOU issue).
    auto file = File(state_file_path, FileOpenMode::FILE_OPEN_READ_ONLY);
    unsealData(file,
               sealing_aad,
               strlen(sealing_aad),
               [&result](std::size_t size) -> void * {
                   if (size != sizeof(State)) {
                       throw EnclaveException("Unseal");
                   }
                   return result.get();
               });
    return result;
}

void store_state(State const & state) {
    auto file = File(state_file_path, FileOpenMode::FILE_OPEN_WRITE_ONLY);
    sealData(file, &state, sizeof(state), sealing_aad, strlen(sealing_aad));
}

/** Using out parameters, as `sizeof(T)` might be a bit large. */
template <typename T>
void read_scalar_from_input(EncryptedDataReader encData,
                            char const * const input_name,
                            T & out)
{
    if (encData.size() != sizeof(T)) {
        throw EnclaveException(std::string("Input <") + input_name
                               + "> has invalid size.");
    }

    encData.decrypt(&out, sizeof(T));
}

/**
   `state` can be modified in-place. This chaining signature is more
   comfortable on the caller side.
 */
State & process_state(State & state,
                      TaskInputs const & inputs,
                      TaskOutputs & outputs,
                      std::vector<std::string> & old_s_files_to_delete,
                      Log & application_log)
{
    // Duff's device. This function is not inlined (but in a separate function
    // instead), so we can use `return` instead of a break (which would be
    // confusing with Duff's device).  Also, the state loading and storing can
    // be handled in a oneliner in the calling function.

    switch (state.state) {
    case State::AWAITING_NEW_NSI_REPORT_REQUESTS: {
        if (!inputs.arguments().empty()) {
            throw InvalidRequest(
                    "No arguments are expected when awaiting a new NSI report "
                    "request, but arguments were supplied");
        }
        return process_nsi_report_request_digestion(state, inputs, application_log);
    }
    case State::AWAITING_NEW_H_FILES:
        /****
         * Cancel/reset request?
         ***/

        if (inputs.argument(arguments::cancel) /* Ignore its value. */) {
            if (inputs.arguments().size() != 1) {
                throw InvalidRequest(
                        "Found the <" + std::string(arguments::cancel) + "> "
                        "argument - when this argument is supplied, no other "
                        "arguments shall be supplied, yet other arguments were "
                        "found");
            }
            return process_cancel(state, old_s_files_to_delete, application_log);
        }

        /****
         * Manual finish report request?
         ***/

        if (inputs.argument(arguments::finish_report) /* Ignore its value. */) {
            if (inputs.arguments().size() != 1) {
                throw InvalidRequest(
                        "Found the <" + std::string(arguments::finish_report) + "> "
                        "argument - when this argument is supplied, no other "
                        "arguments shall be supplied, yet other arguments were "
                        "found");
            }
            return process_manually_finish_report(
                    state, outputs, old_s_files_to_delete, application_log);
        }

        /****
         * H file processing request!
         ***/

        if (!inputs.argument(arguments::file)) {
            throw InvalidRequest(std::string{"Expected argument <"}
                                 + arguments::file + ">, but it is missing");
        }

        if (!inputs.argument(arguments::period)) {
            throw InvalidRequest(std::string{"Expected argument <"}
                                 + arguments::period + ">, but it is missing");
        }

        if (inputs.arguments().size() != 2) {
            throw InvalidRequest(
                    "Found the <" + std::string(arguments::file) + "> and <" +
                    std::string(arguments::period) + "> arguments - when these"
                    " arguments are supplied, no other arguments shall be supplied,"
                    " yet other arguments were found");
        }

        auto const h_file = (*inputs.argument(arguments::file)).toString();
        auto const period_string =
                (*inputs.argument(arguments::period)).toString();
        auto const period_ulong = std::stoul(period_string);
        if (period_ulong > std::numeric_limits<Period>::max()) {
            throw std::out_of_range{"period number too large"};
        }
        auto const given_period = static_cast<Period>(period_ulong);
        return process_h_file(
                state, inputs, outputs, old_s_files_to_delete, application_log, h_file, given_period);
    }     // switch
    assert(false); //
}

State & process_nsi_report_request_digestion(State & state,
                                             TaskInputs const & inputs,
                                             Log & application_log)
{
    // This function searches through the unprocessed NSI report requests for a
    // new valid one to process. Invalid ones are skipped and some diagnostics
    // are written to the application log.

    // Find the right topic.
    try {
        // If it throws, then there is no input data, yet. Otherwise, repeat
        // the call afterwards.
        find_topic(inputs, input_names::nsi_input);
    } catch (...) {
        application_log.append("Waited for new NSI request, nothing came, going back to sleep.\n");
        enclave_printf_log("Waited for new NSI request, nothing came, going back to sleep.");
        return state;
    }
    auto const & nsi_input = find_topic(inputs, input_names::nsi_input);

    bool const there_are_new_inputs =
            state.last_seen_nsi_inputs_topic_size < nsi_input.size();
    if (not there_are_new_inputs) {
        application_log.append("Waited for new NSI request, nothing came, going back to sleep.\n");
        enclave_printf_log("Waited for new NSI request, nothing came, going back to sleep.");
        return state;
    }

    // Search a new, valid NSI report request. Invalid ones are skipped so the
    // enclave does not get stuck.
    auto tmp_report_request = std::unique_ptr<ReportRequest>{new ReportRequest};
    auto id = state.last_seen_nsi_inputs_topic_size;
    auto nsi_input_it = std::next(nsi_input.begin(), id);
    for (; id < nsi_input.size(); ++id, ++nsi_input_it) {
        // If an NSI report cannot be ingested, skip it. There might come
        // a legit one afterwards.
        try {
            // Read the input into a temporary variable, so the current state
            // is not overwritten. If no NSI input fits, we want to write the
            // same state back into the file.
            auto & report_request = *tmp_report_request;
            read_scalar_from_input<ReportRequest>(
                    EncryptedDataReader{*nsi_input_it},
                    input_names::nsi_input,
                    report_request);
            if (not(report_request.first_period <= report_request.last_period)) {
                throw EnclaveException(
                        "Requested period is invalid, because the first period <"
                        + std::to_string(report_request.first_period)
                        + "> is larger than the last period <"
                        + std::to_string(report_request.last_period) + ">");
            }
            if (not(report_request.num_of_census_residents <= report_request.census_residents.size())) {
                throw EnclaveException(
                        "Number of census residents <"
                        + std::to_string(report_request.num_of_census_residents)
                        + "> is larger than allowed <"
                        + std::to_string(report_request.census_residents.size())
                        + ">");
            }
            if (not(report_request.num_of_reference_areas <= report_request.reference_areas.size())) {
                throw EnclaveException(
                        "Number of reference areas <"
                        + std::to_string(report_request.num_of_reference_areas)
                        + "> is larger than allowed <"
                        + std::to_string(report_request.reference_areas.size())
                        + ">");
            }

            // Check that the reference area deserialization works.
            (void)deserialize(PRANGE(report_request.reference_areas.begin(),
                                     report_request.num_of_reference_areas));

            // Commit: We found a valid NSI report request.
            state.go_into_h_processing_state(report_request);
            break;

        } catch (std::exception const & e) {
            application_log.append("Failed to look at NSI report request with data id ");
            application_log.append(std::to_string(id));
            application_log.append(", skipping.\n\tError message: ");
            application_log.append(e.what());
        } catch (...) {
            application_log.append("Failed to look at NSI report request with data id ");
            application_log.append(std::to_string(id));
            application_log.append(", skipping.\n\tError message: (unknown)");
        }
    }

    if (id == nsi_input.size()) {
        application_log.append("No new valid NSI request found.\n");

        // Remember which requests were already viewed (and skipped due to
        // invalidness).
        // Another strategy might be to not progress the state when no valid new
        // request is found, so the invalid requests are logged repeatedly. In
        // this case one would only need to look at the last application log to
        // get an accumulated overview over all invalid NSI report requests
        // instead of downloading all application logs.
        state.last_seen_nsi_inputs_topic_size = nsi_input.size();

        return state;
    } else {
        state.last_seen_nsi_inputs_topic_size = id + 1;
    }

    auto & report_request = state.awaiting_new_h_files.report_request;
    application_log.append("New NSI request arrived.\n");
    log_request_arguments(report_request, application_log);

    enclave_printf_log(
            "New NSI report request arrived for period range %u to %u",
            report_request.first_period,
            report_request.last_period);

    return state;
}

State & process_h_file(State & state,
                       TaskInputs const & inputs,
                       TaskOutputs & outputs,
                       std::vector<std::string> & old_s_files_to_delete,
                       Log & application_log,
                       std::string const & h_file,
                       Period const given_period)
{
    auto & report_request = state.awaiting_new_h_files.report_request;

    auto const next_expected_period = state.awaiting_new_h_files.next_expected_period;
    auto const max_expected_period = report_request.last_period;

    read_h_metadata_file(h_file, application_log);

    if (given_period < next_expected_period
        || given_period > max_expected_period) {
        // This exception prints the parsed `given_period` number, instead
        // of using the actually received argument value. I think this is
        // better because if parsing did something strange, the parsed
        // value can be compared to the original argument which is still
        // accessible through the `displayDfc` action.
        throw InvalidRequest(
                "The received period (" + std::to_string(given_period)
                + ") is not within the range of expected periods ( ["
                + std::to_string(next_expected_period) + " - "
                + std::to_string(max_expected_period) + "] )");
    }

    log_request_arguments(report_request, application_log);
    application_log.append("Expected next period: ");
    application_log.append(std::to_string(next_expected_period));
    application_log.append("\n");

    // Log any skipped periods (6.2.1).
    log_skipped_periods(next_expected_period, given_period, application_log);

    // No problem if this wraps, as it is an unsigned int. In that case,
    // last_period is also uint32_t::max(), hence the analysis will run
    // and the state reset to wait for a report request.
    ++state.awaiting_new_h_files.next_expected_period;

    // Need to use a C-style array here due to the use of SGX SDK APIs.
    uint8_t pseudonymisation_key[PseudonymisationKeyLength];
    {
        auto const & topic = find_topic(inputs, input_names::periodic_pseudonymisation_key);
        PeriodicPseudonymisationKey ppk;
        bool found = false;
        for (auto const & data : topic) {
            // This should not fail, as the only producer is the trusted
            // pseudonymisation enclave, i.e. the size is trusted.
            read_scalar_from_input<PeriodicPseudonymisationKey>(
                    EncryptedDataReader{data},
                    input_names::periodic_pseudonymisation_key,
                    ppk);
            if (ppk.period == given_period) {
                std::copy(std::begin(ppk.pseudonymisation_key),
                          std::end(ppk.pseudonymisation_key),
                          std::begin(pseudonymisation_key));
                found = true;
                break;
            }
        }
        if (!found) {
            throw EnclaveException(
                    "Could not find pseudonymisation key for requested period <"
                    + std::to_string(given_period) + ">");
        }
    }

    auto s_file_in_path = s_file_path(state.s_file_name_index);
    auto s_file_out_path = s_file_path(!state.s_file_name_index);
    // The S file will been written to the other index, so swap it in the state.
    state.s_file_name_index = !state.s_file_name_index;
    // The file we process right now is no longer required when this enclave
    // finishes successfully. (s_file_out_path does not need to be cleaned:
    // in the full analysis it won't be created, and in the state update it is
    // the new state to be consumed in future invocations.)
    old_s_files_to_delete.push_back(s_file_in_path);

    // Make sure the input S file exists, otherwise reading from it later
    // will fail.
    SgxEncryptedFile::create_empty_if_not_exists(s_file_in_path, state.s_file_key);

    SgxFileKey new_s_file_key = {};
    SgxException::throwOnError(
            sgx_read_rand(new_s_file_key.key, sizeof(new_s_file_key.key)),
            "Failed to create a new random S file key");

    constexpr auto one_MiB_buffer = sharemind_hi::enclave::stream::mebibytes(1);

    using namespace full_analysis;
    auto what_to_do = given_period < max_expected_period
                              ? Perform::OnlyStateUpdate
                              : Perform::FullAnalysis;
    uint64_t const start_time = enclave_untrusted_steady_clock_millis();
    full_analysis::run(
            HFileSource(h_file.c_str(),
                        one_MiB_buffer),
            SFileSource(s_file_in_path.c_str(), one_MiB_buffer, state.s_file_key),
            SFileSink(s_file_out_path.c_str(), one_MiB_buffer, new_s_file_key),
            pseudonymisation_key,
            what_to_do,
            // Deserialization might not be required, but this way the code
            // is streamlined and it probably is sub-second effort anyway.
            deserialize(PRANGE(report_request.reference_areas.begin(),
                               report_request.num_of_reference_areas)),
            deserialize(PRANGE(report_request.census_residents.begin(),
                               report_request.num_of_census_residents)),
            report_request.with_calibration,
            outputs,
            application_log);
    uint64_t const end_time = enclave_untrusted_steady_clock_millis();
    state.s_file_key = new_s_file_key;

    // Log how much time the analysis took to run.
    application_log.append("\nRuntime of enclave (not trustworthy): ");
    if (start_time > end_time) { // BC not trusted, you know ...
        // At this point there is already clear that something is wrong ...
        application_log.append("-");
        application_log.append(std::to_string((start_time - end_time) / 1000));
    } else {
        application_log.append(std::to_string((end_time - start_time) / 1000));
    }
    application_log.append("s\n");

    if (what_to_do == Perform::FullAnalysis) {
        state.go_into_request_await_state();
    }

    return state;
}

State & process_cancel(State & state,
                       std::vector<std::string> & old_s_files_to_delete,
                       Log & application_log)
{
    application_log.append("The report generation process was canceled manually.");

    log_request_arguments(state.awaiting_new_h_files.report_request, application_log);

    old_s_files_to_delete.push_back(s_file_path(state.s_file_name_index));
    old_s_files_to_delete.push_back(s_file_path(!state.s_file_name_index));

    state.go_into_request_await_state();

    return state;
}

State & process_manually_finish_report(State & state,
                                       TaskOutputs & outputs,
                                       std::vector<std::string> & old_s_files_to_delete,
                                       Log & application_log)
{
    auto & report_request = state.awaiting_new_h_files.report_request;

    auto const next_expected_period = state.awaiting_new_h_files.next_expected_period;
    auto const max_expected_period = report_request.last_period;

    application_log.append("The report generation process was started manually.");

    log_request_arguments(report_request, application_log);
    application_log.append("Expected next period: ");
    application_log.append(std::to_string(next_expected_period));
    application_log.append("\n");

    // Log any skipped periods (6.2.1).
    log_skipped_periods(next_expected_period,
                        static_cast<uint64_t>(max_expected_period) + 1,
                        application_log);

    // There are no pseudonyms to decrypt, hence we can use a zero key.
    const uint8_t pseudonymisation_key[PseudonymisationKeyLength] = {};

    // Create a dummy H file.
    auto const h_file = persistent_path + "dummy_h_file";
    try {
        File{h_file, sharemind_hi::FileOpenMode::FILE_OPEN_WRITE_ONLY};
    } catch (...) {
        throw EnclaveException("Failed to create a dummy H file, errno: " + std::to_string(errno));
    }

    auto s_file_in_path = s_file_path(state.s_file_name_index);
    auto s_file_out_path = s_file_path(!state.s_file_name_index);
    // The S file will been written to the other index, so swap it in the state.
    state.s_file_name_index = !state.s_file_name_index;
    // The file we process right now is no longer required when this enclave
    // finishes successfully. (s_file_out_path does not need to be cleaned,
    // as it won't be created in the full analysis.)
    old_s_files_to_delete.push_back(s_file_in_path);

    // Make sure the input S file exists, otherwise reading from it later will
    // fail. We do expect that the file exists already, but creating an empty
    // file makes the error handling more consistent.
    SgxEncryptedFile::create_empty_if_not_exists(s_file_in_path, state.s_file_key);

    using namespace full_analysis;
    SgxFileKey new_s_file_key = {};
    // Although not really required to create a new key here, as the file is
    // never written, it does not hurt to still initialize it with randomness.
    SgxException::throwOnError(
            sgx_read_rand(new_s_file_key.key, sizeof(new_s_file_key.key)),
            "Failed to create a new random S file key");

    constexpr auto one_MiB_buffer = sharemind_hi::enclave::stream::mebibytes(1);

    auto h_file_source = HFileSource(h_file.c_str(), one_MiB_buffer);
    auto s_file_source = SFileSource(s_file_in_path.c_str(), one_MiB_buffer, state.s_file_key);

    // The H file must be empty ..
    if (not h_file_source.file_is_exhausted()) {
        throw EnclaveException("Data was found in the empty H dummy file");
    }

    // .. but the S file needs to hold data.
    if (s_file_source.file_is_exhausted()) {
        throw EnclaveException(
                "No data was found in the S file (if you want to cancel the "
                "processing, use the <"
                + std::string(arguments::cancel) + "> argument)");
    }

    uint64_t const start_time = enclave_untrusted_steady_clock_millis();
    full_analysis::run(
            std::move(h_file_source),
            std::move(s_file_source),
            SFileSink(s_file_out_path.c_str(), one_MiB_buffer, new_s_file_key),
            pseudonymisation_key,
            Perform::FullAnalysis,
            deserialize(PRANGE(
                    report_request.reference_areas.begin(),
                    report_request.num_of_reference_areas)),
            deserialize(PRANGE(
                    report_request.census_residents.begin(),
                    report_request.num_of_census_residents)),
            report_request.with_calibration,
            outputs,
            application_log);
    uint64_t const end_time = enclave_untrusted_steady_clock_millis();

    try {
        // Delete the dummy file. Should succeed, but an error is also irrelevant.
        File::remove(h_file);
    } catch (...) {
        /* ignore */
    }

    // Log how much time the analysis took to run.
    application_log.append("\nRuntime of enclave (not trustworthy): ");
    if (start_time > end_time) { // BC not trusted, you know ...
        // At this point there is already clear that something is wrong ...
        application_log.append("-");
        application_log.append(std::to_string((start_time - end_time) / 1000));
    } else {
        application_log.append(std::to_string((end_time - start_time) / 1000));
    }
    application_log.append("s\n");

    state.go_into_request_await_state();

    return state;
}
} // namespace
} // namespace enclave
} // namespace eurostat
