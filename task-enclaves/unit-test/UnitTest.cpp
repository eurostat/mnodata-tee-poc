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

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <sharemind-hi/enclave/common/Log.h>
#include <sharemind-hi/test/Prelude.h>
#include <string>
#include <utility>
#include <vector>
#include "../src/analytics_enclave/Indicators.h"
#include "../src/analytics_enclave/Pseudonymisation.h"

namespace test {
namespace enclave {

char const * TestName = "eurostat_unit_test";

template <typename C>
inline std::string hexBinToString(C const & c)
{
    // Make sure reinterpreting it as uint8_t is ok.
    static_assert(
            std::is_arithmetic<typename std::decay<decltype(*std::begin(c))>::type>::value,
            "");
    std::string result;
    result.reserve(std::distance(std::begin(c), std::end(c))
                   * sizeof(*std::begin(c)));
    uint8_t const * cur = reinterpret_cast<uint8_t const *>(&*std::begin(c));
    uint8_t const * end = reinterpret_cast<uint8_t const *>(&*std::end(c));

    static char syms[] = "0123456789ABCDEF";
    for (; cur != end; ++cur) {
        result += syms[((*cur >> 4) & 0xf)];
        result += syms[*cur & 0xf];
    }
    return result;
};

bool decrypt_pseudonym1() {
    using namespace eurostat::enclave;
    const uint8_t key[PseudonymisationKeyLength] = {
            0x60,
            0x8b,
            0x23,
            0xb7,
            0x23,
            0x63,
            0x0c,
            0x30,
            0x43,
            0x85,
            0xb4,
            0xeb,
            0xd0,
            0x05,
            0x37,
            0x01,
    };
    PseudonymisedUserIdentifier const in = {
            0x13,
            0xbf,
            0xfe,
            0x75,
            0x26,
            0x1b,
            0x0f,
            0xa7,
            0x84,
            0x42,
            0x30,
            0x94,
            0x93,
            0x6b,
            0xa6,
            0xd7,
    };
    UserIdentifier const expected = {
            0x95,
            0xe5,
            0x12,
            0x4f,
            0xa2,
            0x53,
            0x0b,
            0x6b,
            0xec,
            0x01,
            0xff,
            0x60,
    };
    UserIdentifier const result = eurostat::enclave::decrypt_pseudonym(key, in);
    if (result != expected) {
        enclave_printf_log("Failed test %s", __func__);
        enclave_printf_log("expected: <%s>", hexBinToString(expected).c_str());
        enclave_printf_log("result:   <%s>", hexBinToString(result).c_str());
        return false;
    }
    return true;
}

bool decrypt_pseudonym2() {
    using namespace eurostat::enclave;
    const uint8_t key[PseudonymisationKeyLength] = {
        0xf8, 0x02, 0xf9, 0x81, 0x65, 0x4d, 0x24, 0xbb, 0xa8, 0x14, 0x97, 0xa6, 0x2e, 0x8b, 0xa0, 0xbc,
    };
    PseudonymisedUserIdentifier const in = {
        0xae, 0x24, 0xfa, 0xcc, 0x64, 0x06, 0xbf, 0x8f, 0x98, 0xd2, 0xcc, 0x45, 0x1f, 0x3b, 0xa7, 0x3c,
    };
    UserIdentifier const expected = {
        0xaf, 0x55, 0x70, 0xf5, 0xa1, 0x81, 0x0b, 0x7a, 0xf7, 0x8c, 0xaf, 0x4b,
    };
    UserIdentifier const result = eurostat::enclave::decrypt_pseudonym(key, in);
    if (result != expected) {
        enclave_printf_log("Failed test %s", __func__);
        enclave_printf_log("expected: <%s>", hexBinToString(expected).c_str());
        enclave_printf_log("result:   <%s>", hexBinToString(result).c_str());
        return false;
    }
    return true;
}

bool log2histogram() {
    using namespace eurostat::enclave::indicators;
    std::string format_buffer;
    auto formatHistogram = [&](char const * prefix) {
        return Log2HistogramStandardFormatter{prefix, format_buffer};
    };
    auto hist1 = Log2Histogram<5>{};
    auto hist2 = Log2Histogram<5, std::ratio<1,4>>{};
    auto hist3 = Log2Histogram<5, std::ratio<8>>{};

    // A small debugging help to print the correct values, so updating the
    // "correct" (inspected) values is easier.

    auto maybe_print_right_value = [&] {
#if 1
        enclave_printf_log("\nCurrect value:\n%s\n", format_buffer.c_str());
#else
        // nothing.
#endif
    };

    // Format an empty histogram:
    hist1.iterate(formatHistogram("\t\t"));
    maybe_print_right_value();
    if (format_buffer != 
            "\t\t < 1: NA (NA %)\n"
            "\t\t < 2: NA (NA %)\n"
            "\t\t < 4: NA (NA %)\n"
            "\t\t < 8: NA (NA %)\n"
            "\t\t>= 8: NA (NA %)\n"
            ) { return false; }
    format_buffer.clear();

    hist2.iterate(formatHistogram("\t\t"));
    maybe_print_right_value();
    if (format_buffer !=
            "\t\t < 1/4: NA (NA %)\n"
            "\t\t < 1/2: NA (NA %)\n"
            "\t\t < 1: NA (NA %)\n"
            "\t\t < 2: NA (NA %)\n"
            "\t\t>= 2: NA (NA %)\n"
            ) { return false; }
    format_buffer.clear();

    hist3.iterate(formatHistogram("\t\t"));
    maybe_print_right_value();
    if (format_buffer !=
            "\t\t < 8: NA (NA %)\n"
            "\t\t < 16: NA (NA %)\n"
            "\t\t < 32: NA (NA %)\n"
            "\t\t < 64: NA (NA %)\n"
            "\t\t>= 64: NA (NA %)\n"
            ) { return false; }
    format_buffer.clear();

    // Format filled histograms:
    for (int i = 0; i < 35; ++i) {
        hist1(i);
    }
    hist1.iterate(formatHistogram("\t\t"));
    maybe_print_right_value();
    if (format_buffer != 
            "\t\t < 1: 1 (2.9 %)\n"
            "\t\t < 2: 1 (5.7 %)\n"
            "\t\t < 4: 2 (11.4 %)\n"
            "\t\t < 8: 4 (22.9 %)\n"
            "\t\t>= 8: 27 (100.0 %)\n") {
        return false;
    }
    format_buffer.clear();

    hist2(0);
    hist2(0.001);
    for (double i = 0.0625; i < 6; i += 0.0625) {
        hist2(i - 0.01);
        hist2(i);
        hist2(i + 0.01);
    }
    hist2.iterate(formatHistogram("\t\t"));
    maybe_print_right_value();
    if (format_buffer != 
            "\t\t < 1/4: 12 (4.2 %)\n"
            "\t\t < 1/2: 12 (8.4 %)\n"
            "\t\t < 1: 24 (16.7 %)\n"
            "\t\t < 2: 48 (33.4 %)\n"
            "\t\t>= 2: 191 (100.0 %)\n") {
        return false;
    }
    format_buffer.clear();

    hist3(0);
    hist3(0.001);
    for (double i = 1; i < 33; i += 0.5) {
        hist3(i - 0.01);
        hist3(i);
        hist3(i + 0.01);
    }
    hist3.iterate(formatHistogram("\t\t"));
    maybe_print_right_value();
    if (format_buffer != 
            "\t\t < 8: 45 (23.2 %)\n"
            "\t\t < 16: 48 (47.9 %)\n"
            "\t\t < 32: 96 (97.4 %)\n"
            "\t\t < 64: 5 (100.0 %)\n"
            "\t\t>= 64: NA (NA %)\n"
            ) {
        return false;
    }
    format_buffer.clear();

    return true;
}

void main(bool & ok) {
    std::size_t total = 0u;
    std::size_t success = 0u;

    auto count = [&](bool result) {
        ++total;
        success += static_cast<std::size_t>(result);

        enclave_printf("Test %u %s!\n\n", total, (result ? "successful" : "failed"));
    };

    try {
        count(decrypt_pseudonym1());
        count(decrypt_pseudonym2());
        count(log2histogram());

        enclave_printf("Success rate: %u / %u\n", success, total);
        ok = total == success;
    } catch (std::exception const & e) {
        enclave_printf("Exception: %s\n", e.what());
        ok = false;
    } catch (...) {
        enclave_printf("Unknown exception!\n");
        ok = false;
    }
}

} // namespace enclave
} // namespace test

