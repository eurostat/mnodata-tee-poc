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

#include <cstdint>
#include <memory>
#include <sgx_tseal.h>
#include <sharemind-hi/enclave/common/SgxException.h>
#include <sharemind-hi/enclave/common/EnclaveException.h>
#include <vector>

#include "Seal.h"

namespace eurostat {
namespace enclave {
namespace {

constexpr auto const uint32Max = std::numeric_limits<std::uint32_t>::max();
static_assert(std::numeric_limits<std::size_t>::max() > uint32Max, "");

std::vector<std::uint8_t> buffer(std::size_t const size) {
    std::vector<std::uint8_t> buffer;
    buffer.resize(size);
    return buffer;
}
} // anonymous namespace

void sealData(sharemind_hi::enclave::File & outFile,
              void const * const data,
              std::size_t const dataSize,
              void const * const aad,
              std::size_t const aadSize)
{
    ENCLAVE_EXPECT(aadSize <= uint32Max,
                   "Additional authenticated data size too large.");
    ENCLAVE_EXPECT(dataSize <= uint32Max,
                   "Encrypted data size too large.");

    auto const sealedDataSize =
            ::sgx_calc_sealed_data_size(static_cast<std::uint32_t>(aadSize),
                                        static_cast<std::uint32_t>(dataSize));
    ENCLAVE_EXPECT(sealedDataSize < uint32Max,
                   "Failed to calculate sealed data size.");
    auto sealedData = buffer(sealedDataSize);

    sharemind_hi::enclave::SgxException::throwOnError(
            // Use `_ex` to change the key policy to MRENCLAVE (and ProdId), as
            // we want to make sure that only this enclave can read the sealed
            // data, not all the enclaves of the same signer (and ProdId).
            ::sgx_seal_data_ex(
                    SGX_KEYPOLICY_MRENCLAVE,
                    // These magic numbers are named in the developer reference
                    // as the values used by `sgx_seal_data`.
                    {0xFF0000000000000B, 0x0},
                    0xF0000000,
                    static_cast<std::uint32_t>(aadSize),
                    static_cast<std::uint8_t const *>(aad),
                    static_cast<std::uint32_t>(dataSize),
                    static_cast<std::uint8_t const *>(data),
                    sealedDataSize,
                    // Note: the buffer is only implicitly aligned to the
                    // alignment of ::sgx_sealed_data_t, given the malloc
                    // library used within SGX SDK.  Hence we can just
                    // reinterpret the buffer without further alignment
                    // handling (which is also omitted in the SGX SDK example
                    // code).
                    reinterpret_cast<::sgx_sealed_data_t *>(sealedData.data())),
            "Failed to seal data.");
    outFile.write(sealedData.data(), sealedDataSize);
}

void unsealData(sharemind_hi::enclave::File & inFile,
                void const * const expectedAad,
                std::size_t const expectedAadSize,
                std::function<void *(std::size_t)> const allocUnsealedData)
{
    ENCLAVE_EXPECT(expectedAadSize <= uint32Max,
                   "Additional authenticated data size too large.");
    auto const fileSize = inFile.size();
    ENCLAVE_EXPECT(fileSize <= uint32Max, "File size exceeds 32 bits.");

    auto bufferForAad = buffer(expectedAadSize);
    auto encryptedContent = buffer(fileSize);

    inFile.read(encryptedContent.data(), encryptedContent.size());

    // Note: the buffer is only implicitly aligned to the alignment of
    // ::sgx_sealed_data_t, given the malloc library used within SGX SDK.
    // Hence we can just reinterpret the buffer without further alignment
    // handling (which is also omitted in the SGX SDK example code).
    auto const & sealedData =
            *reinterpret_cast<::sgx_sealed_data_t const *>(
                encryptedContent.data());
    auto const decryptedSize = ::sgx_get_encrypt_txt_len(&sealedData);
    ENCLAVE_EXPECT(decryptedSize > 0, "Failed to calculate sealed data size.");

    // Get the buffer for the decrypted data.
    auto * const decryptedBuffer = allocUnsealedData(decryptedSize);

    auto unsealedAadSize(static_cast<std::uint32_t>(expectedAadSize));
    auto unsealedDataSize(static_cast<std::uint32_t>(decryptedSize));

    sharemind_hi::enclave::SgxException::throwOnError(
            ::sgx_unseal_data(&sealedData,
                              bufferForAad.data(),
                              &unsealedAadSize,
                              static_cast<std::uint8_t *>(decryptedBuffer),
                              &unsealedDataSize),
            "Failed to unseal data.");

    // Verify the read aad and data sizes.
    ENCLAVE_EXPECT(unsealedAadSize == expectedAadSize,
                   "Got unexpected amount of sealed additional authenticated "
                   "data.");
    ENCLAVE_EXPECT(unsealedDataSize == decryptedSize,
                   "Got unexpected amount of sealed encrypted data.");

    if (0 != std::memcmp(expectedAad, bufferForAad.data(), expectedAadSize)) {
        throw sharemind_hi::enclave::EnclaveException(
                "Unsealed aad does not match the expected aad. <"
                + std::string{static_cast<char const*>(expectedAad), size_t(expectedAadSize)} + ">, <"
                + std::string{reinterpret_cast<char const*>(bufferForAad.data()), unsealedAadSize} + ">");
    }
}


} // namespace enclave
} // namespace eurostat
