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

#include "Pseudonymisation.h"
#include <array>
#include <sgx_tcrypto.h>
#include <sharemind-hi/common/ArrayTraits.h>
#include <sharemind-hi/enclave/common/EnclaveException.h>
#include <sharemind-hi/enclave/common/Log.h>
#include <sharemind-hi/enclave/common/SgxException.h>

namespace eurostat {
namespace enclave {
UserIdentifier decrypt_pseudonym(PseudonymisationKeyRef pseudonymisation_key,
                                 PseudonymisedUserIdentifier const & in)
{
    // With pseudonymisation - decrypt the pseudonym.

    // `in` will be decrypted into the following struct, as the plaintext
    // is composed of two parts. Makes it easier to use the two parts later
    // on.
    auto decrypted = [&] {
        struct Decrypted {
            UserIdentifier id;
            std::array<std::uint8_t, hmac_bytes> hmac;
        };
        static_assert(sizeof(Decrypted) == sizeof(PseudonymisedUserIdentifier), "");

        Decrypted decrypted;
        uint8_t counter[16] = {};
        sharemind_hi::enclave::SgxException::throwOnError(
                sgx_aes_ctr_decrypt(&pseudonymisation_key,
                                    in.data(),
                                    in.size(),
                                    counter,
                                    static_cast<std::uint32_t>(12u),
                                    reinterpret_cast<std::uint8_t *>(&decrypted)),
                "Failed to decrypt pseudonymised user identifier");
        return decrypted;
    }();

    auto calculated = [&] {
        struct CalculatedHmac {
            std::array<std::uint8_t, hmac_bytes> hmac;
            std::array<std::uint8_t, sha256_size - hmac_bytes> ignored;
        };

        CalculatedHmac calculated;
        sharemind_hi::enclave::SgxException::throwOnError(
                sgx_hmac_sha256_msg(decrypted.id.data(),
                                    decrypted.id.size(),
                                    std::begin(pseudonymisation_key),
                                    sharemind_hi::byteSize(pseudonymisation_key),
                                    reinterpret_cast<std::uint8_t *>(&calculated),
                                    sizeof(calculated)),
                "Calculating the user id hmac after decrypting failed");
        return calculated;
    }();

    if (decrypted.hmac != calculated.hmac) {
        throw sharemind_hi::enclave::EnclaveException(
                "HMAC check failed when reversing pseudonymisation");
    }

    return decrypted.id;
}
} // namespace enclave
} // namespace eurostat
