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

#include "SgxEncryptedFile.h"
#include <array>
#include <cstdint>
#include <sharemind-hi/enclave/common/File.h>
#include <sharemind-hi/enclave/task/stream/Streams.h>
#include <sharemind-hi/enclave/task/stream/Streams_detail.h>

namespace eurostat {
namespace enclave {

/**
  In the eurostat project, some large input files are provided by the host of
  sharemind-hi-server, so they are written directly from the disk in unencrypted
  form.

  F may be sharemind_hi::enclave::File, or SgxEncryptedFile.
 */
template <typename T, typename F>
struct PersistentDataSource {
    using Category = sharemind_hi::enclave::stream::detail::SourceCategory;
    using Out = T;

    static constexpr std::size_t ITEM_SIZE = sizeof(Out);

    template <typename ... Args>
    explicit PersistentDataSource(
            char const * filename,
            std::size_t bufferSizeInBytes,
            Args&& ... args)
        : m_file(filename,
                 // This is a source, so we intend to read it.
                 sharemind_hi::FileOpenMode::FILE_OPEN_READ_ONLY,
                 std::forward<Args>(args)...)
    {
        auto const file_byte_size = m_file.size();

        // The file might be empty, as we also provide empty dummy files to the
        // analysis pipeline for some edge cases.
        ENCLAVE_EXPECT(file_byte_size % ITEM_SIZE == 0u, "Invalid file size. Validate your input data.");
        m_elements_left_in_file = file_byte_size / ITEM_SIZE;
        m_buffer.resize(std::min((bufferSizeInBytes + ITEM_SIZE - 1) / ITEM_SIZE,
                                 m_elements_left_in_file));
        // Trigger a read on the first `next` call.
        m_buffer_index = m_buffer.size();

#ifndef NDEBUG
        enclave_printf_log(
                "PersistentDataSource: Reading %zu elements from file %s",
                m_elements_left_in_file,
                filename);
#endif
    }

    bool next(Out & result) {
        if (m_buffer_index >= m_buffer.size()) {
            if (! fillBufferFromFile()) {
                return false;
            }
        }

        result = m_buffer[m_buffer_index ++];
        return true;
    }

    bool peek(Out & result) {
        enclave_printf_log("HEREE %s %d", __FILE__, __LINE__);
        if (next(result)) {
            --m_buffer_index;
            return true;
        }
        enclave_printf_log("HEREE %s %d", __FILE__, __LINE__);
        return false;
    }

    bool file_is_exhausted() const noexcept { return 0 == m_elements_left_in_file; }

private: /* Methods: */

    bool fillBufferFromFile() {
        if (file_is_exhausted()) {
            return false;
        }

        auto const elementsToRead = std::min(m_buffer.size(), m_elements_left_in_file);
        m_buffer.resize(elementsToRead);
        m_elements_left_in_file -= elementsToRead;

        m_file.read(m_buffer.data(), elementsToRead * ITEM_SIZE);
        m_buffer_index = 0;
        return true;
    }

private: /* Fields: */
    std::vector<Out> m_buffer = {};
    std::size_t m_buffer_index = 0;
    std::size_t m_elements_left_in_file = 0;
    F m_file;
};

/** Allows to stream to a persistent file. The file is opened with
 * sgx_fopen_auto_key(). */
struct PersistentDataSinkBuilder {
    using Category = sharemind_hi::enclave::stream::detail::SinkCategory;

    PersistentDataSinkBuilder(PersistentDataSinkBuilder &&) noexcept = default;

    /**
       buffer_size is handed to std::vector::reserve, so it is up to the STL
       implementation how literally the request is executed.
     */
    explicit PersistentDataSinkBuilder(
            char const * const file_path,
            std::size_t buffer_size,
            SgxFileKey const & key)
        : m_file_path{file_path}, m_buffer_size{buffer_size}, m_key{key}
    {
    }

    template <typename T>
    struct Impl {
        using In = T;
        using Res = void;

        static constexpr std::size_t ITEM_SIZE = sizeof(T);

        Impl(Impl &&) noexcept = default;

        explicit Impl(std::string const & file_path,
                      std::size_t const buffer_size,
                      SgxFileKey const & key)
            : m_file{file_path.c_str(),
                     sharemind_hi::FileOpenMode::FILE_OPEN_WRITE_ONLY,
                     key}
            , m_buffer_size(buffer_size / ITEM_SIZE)
        {
            assert(buffer_size > ITEM_SIZE);
            m_buffer.reserve(buffer_size);
        }

        void sink(T const & item) {
            if (m_buffer.size() >= m_buffer_size) {
                flushChunk();
            }

            m_buffer.push_back(item);
        }

        void finalize() && {
            if (! m_buffer.empty()) {
                flushChunk();
            }

            m_buffer.shrink_to_fit();
            m_file.close();
#ifndef NDEBUG
            enclave_printf_log(
                    "DIAGNOSTICS %zu elements were written to file %s",
                    m_file.bytes_written() / ITEM_SIZE,
                    m_file.filename().c_str());
#endif
        }

        void flushChunk() {
            m_file.write(m_buffer.data(), ITEM_SIZE * m_buffer.size());
            m_buffer.clear();
        }

    private: /* Fields: */
        // Only open the file in this `Impl` class, so the file is only created
        // when we (intent to) write to it.
        SgxEncryptedFile m_file;
        // The implementation for vec::reserve is not required to be exact. Hence,
        // to be exact within this implementation, we have to keep track of the
        // requested buffer_size ourselves. If `reserve` is not exact this could
        // lead to reduced performance (like with BLOCK_SIZE + 1).
        std::size_t m_buffer_size = {};
        std::vector<T> m_buffer = {};
    };

    template <typename T>
    Impl<T> build() && { return Impl<T>{m_file_path, m_buffer_size, m_key}; }

private: /* Fields: */
    std::string m_file_path;
    std::size_t m_buffer_size;
    /** `m_key` Could be a reference with its current usages, but to not get a
     * problem when refactoring, it is a value. */
    SgxFileKey m_key;
};

template <typename Eq, typename Init, typename Sq, typename Builder>
struct SquashBuilder {
    using Category = sharemind_hi::enclave::stream::detail::SinkCategory;

    SquashBuilder(SquashBuilder &&) noexcept = default;

    explicit SquashBuilder(Eq eq, Init init, Sq squash, Builder sb2)
        : m_eq{std::move(eq)}
        , m_init{std::move(init)}
        , m_squash{std::move(squash)}
        , m_builder{std::move(sb2)}
    { }

    template <typename T>
    struct Impl {
        using In = T;
        using Mid = typename std::result_of<Init(T)>::type;
        using Sink = typename Builder::template Impl<Mid>;
        using Res = typename Sink::Res;

        Impl(Impl &&) noexcept = default;

        explicit Impl(Eq eq, Init init, Sq squash, Sink sink)
            : m_eq{std::move(eq)}
            , m_init{std::move(init)}
            , m_squash{std::move(squash)}
            , m_sink{std::move(sink)}
        { }

        void sink(In const & argument) {
            if (m_first) {
                m_first = false;
                m_in = argument;
                m_mid = m_init(argument);
            } else if (!m_eq(m_in, argument)) {
                m_sink.sink(m_mid);
                m_in = argument;
                m_mid = m_init(argument);
            }
            m_squash(m_mid, argument);
        }

        Res finalize() && {
            if (!m_first) { m_sink.sink(m_mid); }
            return std::move(m_sink).finalize();
        }

    private: /* Fields: */
        Eq m_eq;
        Init m_init;
        Sq m_squash;
        /** Make sure the first element does not trigger a group flush. */
        bool m_first = true;
        /** Used to determine whether the element is still in the same group. */
        In m_in;
        /** This is the element to squash all group elements into. */
        Mid m_mid;
        Sink m_sink;
    };

    template <typename T>
    Impl<T> build() && {
        using Mid = typename Impl<T>::Mid;
        return Impl<T>{std::move(m_eq),
                       std::move(m_init),
                       std::move(m_squash),
                       std::move(m_builder).template build<Mid>()};
    }

private: /* Fields: */
    Eq m_eq;
    Init m_init;
    Sq m_squash;
    Builder m_builder;
};

template <typename Eq, typename Init, typename Sq>
struct SquashPipe {
    using Category = sharemind_hi::enclave::stream::detail::PipeCategory;

    template <typename In>
    using Temp = typename std::result_of<Init(In)>::type;

    template <typename In>
    using Out = typename Temp<In>::value_type;

    SquashPipe(SquashPipe &&) noexcept = default;

    explicit SquashPipe(Eq eq, Init init, Sq squash)
        : m_eq{std::move(eq)}
        , m_init{std::move(init)}
        , m_squash{std::move(squash)}
    {}

    template <typename Builder>
    using InBuilder = SquashBuilder<Eq, Init, Sq, Builder>;

    template <typename Builder>
    InBuilder<Builder> build(Builder down) && {
        return InBuilder<Builder>{std::move(m_eq),
                                  std::move(m_init),
                                  std::move(m_squash),
                                  std::move(down)};
    }

private: /* Fields: */
    Eq m_eq;
    Init m_init;
    Sq m_squash;
};

/**
   Squashes consecutive elements of type `I` into one element of type `O` with
   O(1) memory requirements. This is an optimization for the `groupBy +
   flatMap` pattern where flatMap always returns a single element.
   Grouping is done through the `Eq` comparison operator, Initialization
   through the first group element is done through `Init`, and squashing is
   done through `Sq`.
 */
template <
        /** To determine what elements to squash. bool(I const &, I const &) */
        typename Eq,
        /** Initialize the accumulator with the first element to `O`. O(I const &) */
        typename Init,
        /** Squash elements together, also the first element. void(O &, I const &) */
        typename Sq>
inline SquashPipe<Eq, Init, Sq> squash(Eq eq, Init init, Sq squash)
{
    return SquashPipe<Eq, Init, Sq>{
            std::move(eq), std::move(init), std::move(squash)};
}

} // namespace enclave
} // namespace eurostat
