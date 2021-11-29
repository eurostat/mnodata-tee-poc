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

#include "SgxEncryptedFile.h"
#include <cstring>
#include <errno.h>
#include <iterator>
#include <sgx_error.h>
#include <sharemind-hi/enclave/common/EnclaveException.h>
#include <sharemind-hi/enclave/common/Log.h>

namespace eurostat {
namespace enclave {
namespace {

void expect_file_operation(bool const x,
                           SGX_FILE * file,
                           std::string const & filename,
                           std::string const & member_function,
                           char const * failed_function)
{
    if (!x || (file && 0 != sgx_ferror(file))) {
        // Now, something went wrong. *Now* we can check errno.
        constexpr std::size_t errnobuflen = 70;
        char errnobuf[errnobuflen];
        int status = strerror_r(errno, errnobuf, errnobuflen);
        throw sharemind_hi::enclave::EnclaveException(
                "SgxEncryptedFile::" + member_function + "(): <" + failed_function
                + "> failed (managing file " + filename + "): errno: " + errnobuf
                + " (status: " + std::to_string(status) + ")");
    }
}

// Only used within this class, hence we use `m_stream` here.
#define EXPECT_FILE_OPERATION(x, raw_operation) expect_file_operation(x, m_stream.get(), m_filename, __func__, raw_operation)

inline void do_close(SGX_FILE * stream) noexcept {
    assert(stream);
#ifndef NDEBUG
    if (0 != sgx_fclose(stream)) {
        constexpr std::size_t errnobuflen = 70;
        char errnobuf[errnobuflen];
        int status = strerror_r(errno, errnobuf, errnobuflen);
        enclave_printf_log("Failed to close file, errno <%d> %s (strerror_r status, should be 0: %d)", errno, errnobuf, status);
    }
#else
    // Just close and ignore everything else.
    sgx_fclose(stream);
#endif
}

} // namespace

SgxEncryptedFile::SgxEncryptedFile(std::string const & filename,
                                   sharemind_hi::FileOpenMode mode,
                                   SgxFileKey const & key)
    : m_stream{sgx_fopen(
                       filename.c_str(),
                       [&] {
                           using namespace sharemind_hi;
                           if ((FILE_OPEN_READ_ONLY & mode)
                               == FILE_OPEN_READ_ONLY) {
                               return "rb";
                           } else if ((FILE_OPEN_WRITE_ONLY & mode)
                                      == FILE_OPEN_WRITE_ONLY) {
                               return "wb";
                           } else {
                               throw sharemind_hi::enclave::EnclaveException(
                                       "Unsupported file open mode for file <"
                                       + filename + ">");
                           }
                       }(),
                       &key.key),
               do_close}
    , m_filename{filename}
{
    EXPECT_FILE_OPERATION(static_cast<bool>(m_stream), "sgx_fopen");
}

void SgxEncryptedFile::close() noexcept {
    m_stream.reset();
}

std::size_t SgxEncryptedFile::size() {
    // Store the current file position, so we can restore it in the end.
    auto const currentPositionToRestore = tellg();

    // Get the size.
    seekg(0, SEEK_END);
    auto const fileSizeBytes = tellg();

    // Reset to the original position.
    seekg(currentPositionToRestore, SEEK_SET);

    return fileSizeBytes;
}

void SgxEncryptedFile::seekg(std::size_t const pos, int const whence) {
    auto error_code = sgx_fseek(m_stream.get(), pos, whence);
    EXPECT_FILE_OPERATION(error_code == 0, "sgx_fseek");
}

std::size_t SgxEncryptedFile::tellg() {
    auto const fileSizeBytes = sgx_ftell(m_stream.get());
    EXPECT_FILE_OPERATION(fileSizeBytes >= 0, "sgx_ftell");
    return static_cast<std::size_t>(fileSizeBytes);
}

void SgxEncryptedFile::read(void * const dest, std::size_t destSize) {
    constexpr std::size_t const BLOCK_SIZE = SgxEncryptedFile::BLOCK_SIZE;
    auto const begin = static_cast<char *>(dest);
    auto ptr = begin;

    while (destSize != 0u) {
        // Failure condition:
        if (sgx_feof(m_stream.get())) {
            throw sharemind_hi::enclave::EnclaveException(
                    "SgxEncryptedFile::read(): Reached end of file before the "
                    "buffer could be fully filled.");
        }

        auto const bytesToRead = std::min(BLOCK_SIZE, destSize);
        auto const bytesRead = sgx_fread(ptr, 1u, bytesToRead, m_stream.get());
        // Need to call sgx_ferror to detect an error with sgx_fread.
        EXPECT_FILE_OPERATION(true, "sgx_fread");
        // Clear the cache, so old data does not pile up (not sure if this is required, though).
        EXPECT_FILE_OPERATION(sgx_fclear_cache(m_stream.get()) == 0u, "sgx_fclear_cache");

        ptr += bytesRead;
        destSize -= bytesRead;
    }

    m_bytes_read += std::distance(begin, ptr);
}

void SgxEncryptedFile::write(void const * const data, std::size_t const size) {
    constexpr std::size_t const BLOCK_SIZE = SgxEncryptedFile::BLOCK_SIZE;
    auto ptr = static_cast<char const *>(data);
    std::size_t bytesLeft = size;
    while (bytesLeft > 0u) {
        std::size_t const bytesToWrite = std::min(BLOCK_SIZE, bytesLeft);
        EXPECT_FILE_OPERATION(sgx_fwrite(ptr, 1u, bytesToWrite, m_stream.get())
                                      == bytesToWrite,
                              "File write failed.");
        // Clear the cache, so old data does not pile up (not sure if this is
        // required, though).
        EXPECT_FILE_OPERATION(sgx_fclear_cache(m_stream.get()) == 0u,
                              "sgx_fclear_cache");

        bytesLeft -= bytesToWrite;
        ptr += bytesToWrite;
    }
    m_bytes_written += size;
}

void SgxEncryptedFile::remove(std::string const & path) {
    expect_file_operation(
            0 == sgx_remove(path.c_str()), nullptr, "", "remove", "sgx_remove");
}

void SgxEncryptedFile::create_empty_if_not_exists(std::string const & path,
                                                  SgxFileKey const & key)
try {
    // Try to open it in read mode which fails if it does not exist.
    SgxEncryptedFile{path, sharemind_hi::FileOpenMode::FILE_OPEN_READ_ONLY, key};
} catch (std::bad_alloc const &) {
    // It was not a missing file. So don't delete it.
    throw;
} catch (...) {
    // Open the file in write mode so it will be created.
    SgxEncryptedFile{path, sharemind_hi::FileOpenMode::FILE_OPEN_WRITE_ONLY, key};
}

} // namespace enclave
} // namespace sharemind_hi
