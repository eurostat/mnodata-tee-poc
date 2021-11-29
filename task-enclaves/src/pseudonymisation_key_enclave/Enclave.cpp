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

#include "Entities.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <sgx_error.h>
#include <sgx_tcrypto.h>
#include <sgx_trts.h>
#include <sharemind-hi/enclave/common/EnclaveException.h>
#include <sharemind-hi/enclave/common/EncryptedData.h>
#include <sharemind-hi/enclave/common/Log.h>
#include <sharemind-hi/enclave/task/Messages.h>
#include <sharemind-hi/enclave/task/Task.h>
#include <sharemind-hi/enclave/task/TaskIO.h>
#include <string>

namespace eurostat {
namespace enclave {
namespace {
using namespace sharemind_hi;
using namespace sharemind_hi::enclave;

using topic_name_t = char const *;
using argument_name_t = char const *;

namespace input_names {
constexpr topic_name_t periodic_pseudonymisation_key = "periodic_pseudonymisation_key";
} // namespace input_names

namespace argument_names {

constexpr argument_name_t period = "period";

} // namespace argument_names

std::list<EnclaveDataInfo> const * find_topic(TaskInputs const & inputs,
                                              char const * const name)
{
    auto const & topics = inputs.inputs();
    for (auto const & topic : topics) {
        if (topic.name.toString() == name) { return &topic.data; }
    }
    return nullptr;
}

std::vector<PeriodicPseudonymisationKey> get_all_existing_periodic_keys(TaskInputs const & inputs)
{
    std::vector<PeriodicPseudonymisationKey> out;

    auto const * periodic_keys = find_topic(inputs, input_names::periodic_pseudonymisation_key);

    if (!periodic_keys) {
#ifndef NDEBUG
        enclave_printf_log("No previous periodic keys found...");
#endif
        return out;
    }

    for (auto const & period : *periodic_keys) {
        EncryptedDataReader r{period};
        PeriodicPseudonymisationKey k;
        if (r.size() != sizeof(k)) {
            throw new EnclaveException("Stored period key is wrong in size!");
        }
        r.decrypt(&k, sizeof(k));
#ifndef NDEBUG
        // This would be rather noisy in production.
        enclave_printf_log("Found periodic-pseudon. key for #%u", k.period);
#endif
        out.push_back(k);
    }

    return out;
}

PeriodicPseudonymisationKey
generate_new_key(unsigned long period,
                 std::vector<PeriodicPseudonymisationKey> const & existing_keys)
{
    bool const period_is_already_present =
            std::any_of(existing_keys.begin(),
                        existing_keys.end(),
                        [&](PeriodicPseudonymisationKey const & k) {
                            return k.period == period;
                        });

    if (period_is_already_present) {
        throw EnclaveException("The period <" + std::to_string(period) + "> is already present");
    }

    PeriodicPseudonymisationKey new_key{};
    if (sgx_read_rand(new_key.pseudonymisation_key,
                      byteSize(new_key.pseudonymisation_key))
        != SGX_SUCCESS) {
        throw new EnclaveException("Periodic key generation failed");
    }
    new_key.period = period;
    return new_key;
}

} // namespace
} // namespace enclave
} // namespace eurostat


namespace sharemind_hi {
namespace enclave {

using namespace eurostat::enclave;

void run(TaskInputs const & inputs, TaskOutputs & outputs) {

    /************
     * Argument parsing
     ***********/

    auto period_argument = inputs.argument(argument_names::period);

    if (!period_argument) {
        throw new EnclaveException("The periodical pseudonymisation key generation <period> argument is not defined");
    }

    if (inputs.arguments().size() != 1) {
        // Here we know that the <period> argument was provided, so there must
        // be other, invalid arguments.
        throw new EnclaveException("The pseudonymisation enclave expects exactly one argument <period>, but more were provided");
    }

    auto const period = std::stoul((*period_argument).toString().c_str());

    if (period > std::numeric_limits<Period>::max()) {
        throw EnclaveException(
                "Period <" + std::to_string(period)
                + "> is larger than the allowed maximum value "
                + std::to_string(std::numeric_limits<Period>::max()));
    }

    enclave_printf_log("Period: %lu", period);

    /************
     * Computing
     ***********/

    auto const new_key =
            generate_new_key(period, get_all_existing_periodic_keys(inputs));

    /************
     * Output creation
     ***********/

    outputs.put(input_names::periodic_pseudonymisation_key,
                &new_key,
                sizeof(new_key));
}
} // namespace enclave
} // namespace sharemind_hi
