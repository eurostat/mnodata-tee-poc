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

#include <cstddef>
#include <sgx_key.h>
#include <string>
#include <sgx_tprotected_fs.h>
#include <sharemind-hi/filesystem/FileOpenMode.h>


namespace eurostat {
namespace enclave {

/** A wrapper struct to unlock the copy operator. Not using `std::array<>` as
 * we need a plain C array for the SGX SDK.*/
struct SgxFileKey {
    ::sgx_key_128bit_t key;
};

class SgxEncryptedFile {
public:
    constexpr static std::size_t const BLOCK_SIZE = 0x100000; // 1MiB
    static_assert(BLOCK_SIZE <= 2ull * 1024 * 1024 * 1024 /* 2GiB */,
                  "SGX SDK functions silently fail when writing large blocks "
                  "of data at once.");

public: /* methods: */
    SgxEncryptedFile(SgxEncryptedFile &&) noexcept = default;
    SgxEncryptedFile(SgxEncryptedFile const &) = delete;
    SgxEncryptedFile(SGX_FILE * const stream);
    SgxEncryptedFile(std::string const & filename,
                     sharemind_hi::FileOpenMode,
                     SgxFileKey const &);

    SgxEncryptedFile & operator=(SgxEncryptedFile &&) noexcept = default;
    SgxEncryptedFile & operator=(SgxEncryptedFile const &) = delete;

    void close() noexcept;

    SGX_FILE * stream() const { return m_stream.get(); }

    // Does 4 ocalls.
    std::size_t size();
    void seekg(std::size_t pos, int whence);
    std::size_t tellg();
    /** Reads exactly the requested amount of data, or throws if the buffer
     cannot be filled. */
    void read(void * buffer, std::size_t size);
    void write(void const * buffer, std::size_t size);

    // Solely for diagnostics.
    std::string const & filename() const noexcept { return m_filename; }
    /** The returned value may not be accurate in case of exceptions. */
    std::size_t bytes_read() const noexcept { return m_bytes_read; }
    /** The returned value may not be accurate in case of exceptions. */
    std::size_t bytes_written() const noexcept { return m_bytes_written; }

public: /* static methods: */

    static void rename(std::string const & oldpath, std::string const & newpath);
    static void remove(std::string const & path);

    static void create_empty_if_not_exists(std::string const & path,
                                           SgxFileKey const &);

private: /* fields: */
    std::unique_ptr<SGX_FILE, void(*)(SGX_FILE*)> m_stream;

    // Solely for diagnostics.
    std::string m_filename;
    std::size_t m_bytes_read = 0;
    std::size_t m_bytes_written = 0;
};

} // namespace enclave
} // namespace eurostat
