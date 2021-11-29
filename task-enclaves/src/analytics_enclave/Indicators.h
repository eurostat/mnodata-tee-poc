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

#include "Entities.h"
#include "SgxEncryptedFile.h"
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ratio>
#include <sharemind-hi/enclave/common/File.h>
#include <sharemind-hi/enclave/task/stream/Streams.h>
#include <sharemind-hi/enclave/task/stream/Streams_detail.h>

namespace eurostat {
namespace enclave {
namespace indicators {

inline double pow2(double const e)
{
    return e * e;
}

constexpr double constexpr_ldexp(double num, uint8_t exp)
{
    return exp == 0 ? num : 2 * constexpr_ldexp(num, exp - 1);
}

// Using ptr-to-member as arg instead of template argument, so the caller
// side is easier.
template <typename I, typename O>
std::array<O, num_subperiods> map(std::array<I, num_subperiods> const & in,
                                  O I::*ptr)
{
    std::array<O, num_subperiods> result;
    for (std::size_t subperiod = 0; subperiod < num_subperiods; ++subperiod) {
        result[subperiod] = in[subperiod].*ptr;
    }
    return result;
}

static_assert(constexpr_ldexp(1.0 / 256.0, 0) == 1.0 / 256.0, "");
static_assert(constexpr_ldexp(1.0 / 256.0, 1) == 1.0 / 128.0, "");
static_assert(constexpr_ldexp(1.0 / 256.0, 2) == 1.0 / 64.0, "");
static_assert(constexpr_ldexp(1.0 / 256.0, 3) == 1.0 / 32.0, "");
static_assert(constexpr_ldexp(1.0 / 256.0, 4) == 1.0 / 16.0, "");

constexpr bool is_power_of_2(uint64_t const num) {
    return num && !(num & (num - 1));
}

static_assert(!is_power_of_2(0), "");
static_assert(is_power_of_2(1), "");
static_assert(is_power_of_2(2), "");
static_assert(!is_power_of_2(3), "");
static_assert(is_power_of_2(4), "");
static_assert(!is_power_of_2(5), "");

/**
  Type of the argument for the Log2Histogram::iterate callback. Without lambda
  auto arguments the argument must be named which is tedious if the type is
  associated to the templated class, hence it is kept outside of Log2Histogram.
 */
struct IterateArg {
    /** If `last == true`, then this is the value from the previous bin. */
    std::uint64_t numerator;
    /** If `last == true`, then this is the value from the previous bin. */
    std::uint64_t denominator;
    /** K-anonymized. */
    std::uint64_t count;
    float cumulative_percentage;
    enum class BinType { RegularBin, LastCatchAllBin };
    BinType bin_type;
};

inline void k_anonymize(std::uint64_t const value, std::string & output)
{
    if (value < indicators_k_anonymity) {
        output.append(indicators_k_anonymity_replacement);
    } else {
        output.append(std::to_string(value));
    }
}

inline void k_anonymize_percentage(std::uint64_t const value, double percentage, std::string & output)
{
    if (value < indicators_k_anonymity) {
        output.append(indicators_k_anonymity_replacement);
        output.append(" %");
    } else {
        constexpr std::size_t buf_size = 100;
        char buf [buf_size] = {'\0'};
        snprintf(buf, buf_size, "%.1f %%", percentage);
        output.append(buf);
    }
}

inline std::uint64_t k_anonymize(std::uint64_t const value)
{
    if (value < indicators_k_anonymity) {
        return 0;
    } else {
        return value;
    }
}

/**
    \arg Bins: At least two bins are required. The last bin is a "catch-all" bin for
              values than the previous bins.
    \arg LowestBinValue_Ratio: A `std::ratio<., .>` which is actually a power of 2
                               (with positive or negative exponent), but the
                               `std::ratio<., .>` form was chosen to make reading
                               of the usage side easier.
 */
template <std::size_t Bins, typename /* std::ratio */ LowestBinValue_Ratio = std::ratio<1>>
class Log2Histogram {

private: /* Types: */
    // We could also just take the exponent instead of the std::ratio, but on
    // the caller side the ratio is much more understandable than the exponent,
    // hence we instead put an enforcement here: `num` or `den` must be 1, and
    // the other one must be a power of 2.
    static_assert((LowestBinValue_Ratio::num == 1
                   && is_power_of_2(LowestBinValue_Ratio::den))
                          || (is_power_of_2(LowestBinValue_Ratio::num)
                              && LowestBinValue_Ratio::den == 1),
                  "Only a power of 2 is allowed.");

    static_assert(Bins > 1, "");

    static constexpr double lowest_bin_value =
            static_cast<double>(LowestBinValue_Ratio::num)
            / static_cast<double>(LowestBinValue_Ratio::den);

    // If `Bins` == 2, then `lowest_bin_value == highest_bin_value`
    static constexpr double highest_bin_value =
            constexpr_ldexp(lowest_bin_value, Bins - 2);

    static constexpr double normalized_lowest_bin_value = 1.0;

    // If `Bins` == 2, then `normalized_lowest_bin_value == normalized_highest_bin_value`
    static constexpr double normalized_highest_bin_value =
            constexpr_ldexp(normalized_lowest_bin_value, Bins - 2);

public: /* Methods: */
    void operator()(double const non_normalized_number) noexcept {
        if (!std::isfinite(non_normalized_number)) {
            // Note: This should only happen if `S` has been tampered with and
            // garbage is read. In that case, the calculation would fail resulting
            // in UB (m_data[bin] out of bounds), hence we std::abort here.
            enclave_printf_log("Log2Histogram found non-finite number. Has the S file been tampered with? Aborting.");
            std::abort();
        }
        if (non_normalized_number < lowest_bin_value) {
            ++m_data[0];
            return;
        }

        auto const number = static_cast<std::uint64_t>(std::min(
                std::floor(non_normalized_number / lowest_bin_value), normalized_highest_bin_value));

        // `clz(0)` is undefined.
        assert(number > 0);

        // Example: 0100 0000 -> clz == 1; sizeof*CHAR=8; want bin == 7;
        //  -> 8 - 1 == 7
        // Example: 0000 0100 -> clz == 1; sizeof*CHAR=8; want bin == 3;
        //  -> 8 - 5 == 3
        // Example: 0000 0001 -> clz == 7; sizeof*CHAR=8; want bin == 1;
        //  -> 8 - 7 == 1
        auto const bin = (sizeof(number) * CHAR_BIT) - __builtin_clzl(number);
        assert(bin < m_data.size());
        ++m_data[bin];
    }

    /**
       \arg callback: void(IterateArg); E.g. Log2HistogramStandardFormatter.
       \return true if the callback was called at least once, otherwise false.
      */
    template <typename F /* void(IterateArg) */>
    void iterate(F callback) const {
        std::uint64_t num = LowestBinValue_Ratio::num;
        std::uint64_t den = LowestBinValue_Ratio::den;

        using BinType = IterateArg::BinType;

        // `total` for the rolling percentage calculation. 
        std::uint64_t total = 0;
        for (auto const & d : m_data) {
            total += d;
        }
        if (total == 0) {
            // Empty. this will show NA everywhere, so the value of `total` is
            // irrelevant as long as it is not 0, as it is used for division.
            total = 1;
        }

        std::uint64_t previous_num = 0;
        std::uint64_t previous_den = 0;
        std::uint64_t rolling_sum = 0;
        for (std::size_t bin = 0; bin < Bins; ++bin) {
            rolling_sum += m_data[bin];
            float const rolling_percentage = (static_cast<double>(rolling_sum)
                                              / static_cast<double>(total))
                                             * 100.;
            if (bin < Bins - 1) {
                callback(IterateArg{num,
                                    den,
                                    m_data[bin],
                                    rolling_percentage,
                                    BinType::RegularBin});
            } else {
                // Very last iteration, the catch-all bin.
                callback(IterateArg{previous_num,
                                    previous_den,
                                    m_data[bin],
                                    rolling_percentage,
                                    BinType::LastCatchAllBin});
            }

            previous_num = num;
            previous_den = den;

            if (den > 1) {
                den /= 2;
            } else {
                assert(den == 1);
                assert(num >= 1);
                num *= 2;
            }
        }
    }

private: /* Fields: */
    std::array<std::uint64_t, Bins> m_data = {};
};

class Log2HistogramStandardFormatter {
public:
    Log2HistogramStandardFormatter(char const * const prefix, std::string & output)
        : m_prefix(prefix), m_output(output)
    {}
    Log2HistogramStandardFormatter(Log2HistogramStandardFormatter const &) noexcept = default;

    void operator()(indicators::IterateArg const & arg)
    {
        m_output.append(m_prefix);
        using BinType = indicators::IterateArg::BinType;
        switch (arg.bin_type) {
            case BinType::RegularBin: m_output.append(" < "); break;
            case BinType::LastCatchAllBin: m_output.append(">= "); break;
        }
        m_output.append(std::to_string(arg.numerator));
        if (arg.denominator > 1) {
            m_output.append("/");
            m_output.append(std::to_string(arg.denominator));
        }
        m_output.append(": ");
        // Note: arg.count is already k-anonymized. But for consistent "N/A"
        // printing, pipe it also here through that function.
        k_anonymize(arg.count, m_output);
        m_output.append(" (");
        k_anonymize_percentage(arg.count, arg.cumulative_percentage, m_output);
        m_output.append(")");
        m_output.append("\n");
    }

private:
    char const * m_prefix;
    std::string & m_output;
};

/** 6.4.3 */
class Count {
public: /* Types: */
    struct Data {
        std::uint64_t num_records = {};
        std::uint64_t num_unique_users = {};
        Log2Histogram<10> histogram_records_per_user = {};
    };

public: /* Methods: */
    Data finish() noexcept
    {
        if (m_group_size > 0) { finish_group(); }
        return m_data;
    }

    void operator()(UserIdentifier const & id) noexcept
    {
        ++m_data.num_records;

        if (m_group_size == 0) {
            // This is the verify first element we encounter.
            start_group(id);

            return;
        } else if (id == m_group_representative) {
            // Same group
            ++m_group_size;
        } else {
            // New group started.
            finish_group();

            // Reset group.
            start_group(id);
        }
    }

private: /* Methods: */
    void start_group(UserIdentifier const & id) noexcept
    {
        m_group_representative = id;
        m_group_size = 1;
        ++m_data.num_unique_users;
    }

    void finish_group() noexcept
    {
        assert(m_group_size > 0);
        m_data.histogram_records_per_user(m_group_size);
    }

private: /* Fields: */
    Data m_data = {};
    UserIdentifier  m_group_representative = {};
    /** Only 0 in the very beginning, then always at least 1. */
    std::uint64_t m_group_size = 0;
};

/** 6.4.4 */
struct SpatiotemporalDistribution {
    std::array<std::uint64_t, 16> result = {};

    void operator()(IColumn const & col) {
        std::size_t index = 0;
        // Should be ordered in a way that all `value[subperiod 0]==0` entries
        // are ordered together, because most should be 0. Hence make it
        // represent the highest bit.
        if (col[0] != 0) { index += 8; }
        if (col[1] != 0) { index += 4; }
        if (col[2] != 0) { index += 2; }
        if (col[3] != 0) { index += 1; }
        ++result[index];
    }
};

/** 6.4.5 */
namespace spatial_distribution {
class HHistogramCountOfUniqueTilesPerUserWithPresence {
private: /* Types: */
    using H = UserFootprintUpdates;
    using Histogram = Log2Histogram<10>;
    struct Data {
        Histogram histogram = {};
        std::uint64_t num_tiles_with_presence = 0;
    };

public: /* Methods: */
    void operator()(H const & e) noexcept
    {
        if (m_first_incovation) {
            start_user(e.key.id);
            m_first_incovation = false;
        } else if (e.key.id != m_user_id) {
            // New user.
            finish_user();

            start_user(e.key.id);
        }

        for (std::size_t i = 0; i < num_subperiods; ++i) {
            if (e.i_column[i] > 0) {
                ++m_datas[i].num_tiles_with_presence;
            }
        }
    }

    std::array<Histogram, num_subperiods> finish() noexcept
    {
        if (!m_first_incovation) { finish_user(); }
        return map(m_datas, &Data::histogram);
    }

private: /* Methods: */
    void finish_user() noexcept {
        assert(!m_first_incovation);
        for (auto & data : m_datas) {
            if (data.num_tiles_with_presence > 0) {
                data.histogram(data.num_tiles_with_presence);
            }
        }
    }

    void start_user(UserIdentifier const & id) noexcept
    {
        m_user_id = id;

        for (auto & data : m_datas) {
            data.num_tiles_with_presence = 0;
        }
    }

private: /* Fields: */
    bool m_first_incovation = true;
    std::array<Data, num_subperiods> m_datas = {};
    /**
       User id of the current user. We know that (Id, tile_index) values
       are unique, hence each application also has a new tile_index, hence
       this can be omitted.
     */
    UserIdentifier m_user_id = {};
};

class HistogramOfWeightValues {
private: /* Types: */
    using Histogram = Log2Histogram<17, std::ratio<1, 256>>;

public: /* Methods: */
    void operator()(IColumn const & col) noexcept
    {
        for (std::size_t i = 0; i < num_subperiods; ++i) {
            m_histograms[i](col[i]);
        }
    }

    std::array<Histogram, num_subperiods> finish() const noexcept
    {
        return m_histograms;
    }

private: /* Fields: */
    std::array<Histogram, num_subperiods> m_histograms = {};
};

class HistogramOfAverageDistances {
private: /* Types: */
    using H = UserFootprintUpdates;
    using S = AccumulatedUserFootprint;
    using Histogram = Log2Histogram<10, std::ratio<256>>;

    struct Data {
        Histogram histogram;

        struct Mean {
            double e = 0;
            double n = 0;
            double weight_sum = 0;
        };
        Mean h_mean;
        Mean s_mean;
    };

public: /* Methods: */
    void operator()(H const & e) noexcept
    {
        process(e.key, e.i_column, &Data::h_mean);
    }

    void operator()(S const & e) noexcept
    {
        process(e.key, e.i_column, &Data::s_mean);
    }

    std::array<Histogram, num_subperiods> finish() noexcept
    {
        if (!m_first_incovation) { finish_user(); }
        return map(m_datas, &Data::histogram);
    }

private: /* Methods: */
    void process(FootprintKey const & key,
                 IColumn const & col,
                 Data::Mean Data::*mean) noexcept
    {
        if (m_first_incovation) {
            start_user(key.id);

            m_first_incovation = false;
        } else if (key.id != m_user_id) {
            // New user.
            finish_user();

            start_user(key.id);
        }

        for (std::size_t i = 0; i < num_subperiods; ++i) {
            (m_datas[i].*mean).e += col[i] * key.tile.easting;
            (m_datas[i].*mean).n += col[i] * key.tile.northing;
            (m_datas[i].*mean).weight_sum += col[i];
        }
    }

    void start_user(UserIdentifier const & id) noexcept
    {
        m_user_id = id;

        for (auto & data : m_datas) {
            data.h_mean = {};
            data.s_mean = {};
        }
    }

    void finish_user() noexcept
    {
        assert(!m_first_incovation);
        for (auto & data : m_datas) {
            // Don't look at this record if there is no presence in H or old S.
            if (data.h_mean.weight_sum == 0 || data.s_mean.weight_sum == 0) { continue; }

            auto const h_mean_e = data.h_mean.e / data.h_mean.weight_sum;
            auto const h_mean_n = data.h_mean.n / data.h_mean.weight_sum;

            auto const s_mean_e = data.s_mean.e / data.s_mean.weight_sum;
            auto const s_mean_n = data.s_mean.n / data.s_mean.weight_sum;

            auto const distance = std::sqrt(pow2(h_mean_e - s_mean_e)
                                            + pow2(h_mean_n - s_mean_n));

            data.histogram(distance);
        }
    }

private: /* Fields: */
    bool m_first_incovation = true;
    UserIdentifier m_user_id = {};
    std::array<Data, num_subperiods> m_datas = {};
};

class BoundingBoxMeasure {
public: /* Types: */
    using Histogram = Log2Histogram<8, std::ratio<1024>>;
    struct Result {
        Histogram h_diagonal_length_histogram;
        Histogram old_s_diagonal_length_histogram;
        Histogram old_s_vs_new_s_diagonal_length_histogram;
    };

private: /* Types: */
    using H = UserFootprintUpdates;
    using S = AccumulatedUserFootprint;

    struct BoundingBox {
        TileIndex low = {
                std::numeric_limits<decltype(TileIndex::easting)>::max(),
                std::numeric_limits<decltype(TileIndex::northing)>::max(),
        };
        TileIndex high = {
                std::numeric_limits<decltype(TileIndex::easting)>::min(),
                std::numeric_limits<decltype(TileIndex::northing)>::min(),
        };

        /** If `!(low <= high)`, then `-1.0` is returned. */
        double diagonal_length() {
            if(low.easting > high.easting || low.northing > high.northing) {
                return -1.0;
            } else {
                return std::sqrt(pow2(high.easting - low.easting)
                                 + pow2(high.northing - low.northing));
            }
        }
    };

    struct Data {
        BoundingBox h_bb;
        BoundingBox old_s_bb;
        BoundingBox new_s_bb;

        Result result;
    };

public: /* Methods: */
    void h(H const & e) noexcept
    {
        process(e.key, e.i_column, &Data::h_bb);
    }

    void old_s(S const & e) noexcept
    {
        process(e.key, e.i_column, &Data::old_s_bb);
    }

    void new_s(S const & e) noexcept
    {
        process(e.key, e.i_column, &Data::new_s_bb);
    }

    std::array<Result, num_subperiods> finish() noexcept
    {
        if (!m_first_incovation) { finish_user(); }
        return map(m_datas, &Data::result);
    }

private: /* Methods: */
    void process(FootprintKey const & key,
                 IColumn const & col,
                 BoundingBox Data::*bb_ptr) noexcept
    {
        if (m_first_incovation) {
            start_user(key.id);
            m_first_incovation = false;
        } else if (key.id != m_user_id) {
            finish_user();
            start_user(key.id);
        }

        for (std::size_t subperiod = 0; subperiod < num_subperiods; ++subperiod) {
            if (col[subperiod] == 0.0) { continue; }

            BoundingBox & bb = m_datas[subperiod].*bb_ptr;

            bb.low.easting = std::min(bb.low.easting, key.tile.easting);
            bb.low.northing = std::min(bb.low.northing, key.tile.northing);

            bb.high.easting = std::max(bb.high.easting, key.tile.easting);
            bb.high.northing = std::max(bb.high.northing, key.tile.northing);
        }
    }

    void start_user(UserIdentifier const & id) noexcept
    {
        m_user_id = id;

        for (auto & data : m_datas) {
            data.h_bb = {};
            data.old_s_bb = {};
            data.new_s_bb = {};
        }
    }

    void finish_user() noexcept
    {
        assert(!m_first_incovation);
        for (std::size_t subperiod = 0; subperiod < num_subperiods; ++subperiod) {
            auto & data = m_datas[subperiod];

            // In case no element was added, "-1.0" is returned as a "missing"
            // value (std::optional).
            auto const h_diagonal_length = data.h_bb.diagonal_length();
            auto const old_s_diagonal_length = data.old_s_bb.diagonal_length();
            auto const new_s_diagonal_length = data.new_s_bb.diagonal_length();

            if (h_diagonal_length >= 0.0) {
                data.result.h_diagonal_length_histogram(h_diagonal_length);
            }
            if (old_s_diagonal_length >= 0.0) {
                data.result.old_s_diagonal_length_histogram(old_s_diagonal_length);
            }
            if (old_s_diagonal_length >= 0.0 && new_s_diagonal_length >= 0.0) {
                data.result.old_s_vs_new_s_diagonal_length_histogram(
                        std::abs(old_s_diagonal_length - new_s_diagonal_length));
            }
        }
    }

private: /* Fields: */
    bool m_first_incovation = true;
    UserIdentifier m_user_id = {};

    std::array<Data, num_subperiods> m_datas = {};
};

} // namespace spatial_distribution

} // namespace indicators
} // namespace enclave
} // namespace eurostat
