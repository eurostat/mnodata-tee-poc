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

#include "FullAnalysis.h"
#include "Entities.h"
#include "Indicators.h"
#include "Parameters.h"
#include "Pseudonymisation.h"
#include "Xoroshiro.h"
#include <bitset>
#include <cstdint>
#include <iterator>
#include <string>

#define RANGE(...) std::begin(__VA_ARGS__), std::end(__VA_ARGS__)

namespace eurostat {
namespace enclave {
namespace full_analysis {

using sharemind_hi::enclave::stream::mebibytes;

namespace {

using namespace sharemind_hi::enclave::stream;

// One char names, here we go ...
using H = UserFootprintUpdates;
using S = AccumulatedUserFootprint;
using Y = QuantisedFootprint;

/**
   This class encapsulates indicators / measurements / counts that will later be
   logged into the application log.
 */
class Indicators {
public: /* Methods: */
    Indicators(Log & application_log) noexcept
        : m_application_log(application_log)
    {}

    void report_additional_H_duplicates(std::uint64_t const additional_duplicates) noexcept {
        m_number_of_duplicate_H_records += additional_duplicates;
    }

    void process_h_record(H const & e)
    {
        m_h_count(e.key.id);
        m_spatiotemporal_distribution(e.i_column);
        m_h_unique_tiles_per_user_with_presence(e);
        m_h_weight_values(e.i_column);
        m_average_distances(e);
        m_bounding_box_measure.h(e);
    }

    void process_s_old_record(S const & e)
    {
        m_s_old_count(e.key.id);
        m_s_old_weight_values(e.i_column);
        m_average_distances(e);
        m_bounding_box_measure.old_s(e);
    }

    void process_s_new_record(S const & e)
    {
        m_s_new_count(e.key.id);
        m_bounding_box_measure.new_s(e);
    }

    ~Indicators()
    {
        auto const h_count = m_h_count.finish();
        auto const s_old_count = m_s_old_count.finish();
        auto const s_new_count = m_s_new_count.finish();

        m_application_log.append("\n");

        if (std::min(h_count.num_unique_users, s_new_count.num_unique_users)
            < indicators_k_anonymity * 1000) {
            // Enough unique users must be present to lessen the risk that the
            // masked NA values can be reconstructed from the rolling
            // percentage numbers. `1000`: The histogram rolling percentage
            // is printed with one digit after the decimal point (`100.0 %`).
            m_application_log.append("The indicators are omitted because the user count is too small.\n");
            return;
        }

        auto formatHistogram = [&](char const * const prefix) {
            return indicators::Log2HistogramStandardFormatter{prefix,
                                                              m_application_log};
        };

        {
            m_application_log.append("Number of duplicate records in the H file: ");
            m_application_log.append(std::to_string(m_number_of_duplicate_H_records));
            m_application_log.append("\n");
        }

        m_application_log.append("\n");

        {
            struct Data {
                char const * what;
                indicators::Count::Data const & count;
            };
            for (const auto data : {Data{"H", h_count}, Data{"Old S", s_old_count}, Data{"New S", s_new_count}}) {
                auto const & r = data.count;
                m_application_log.append(data.what);
                m_application_log.append(":");
                m_application_log.append("\n\tNumber of unique users in file: ");
                m_application_log.append(std::to_string(r.num_unique_users));
                m_application_log.append("\n\tNumber of records in file: ");
                m_application_log.append(std::to_string(r.num_records));
                m_application_log.append("\n\tHistogram of Number of records per user:\n");
                r.histogram_records_per_user.iterate(formatHistogram("\t\t"));
            }
        }

        m_application_log.append("\n");

        {
            auto const r = m_spatiotemporal_distribution.result;
            m_application_log.append("Histogram: count of H records with given subperiod pattern (subperiod order in pattern 0,1,2,3). 0 in pattern position i means given subperiod i had weight 0 in given record, 1 means weight >0. :\n");
            for (std::size_t i = 0; i < r.size(); ++i) {
                m_application_log.append("\t");

                // writes for example "1101" or "0100"
                m_application_log.append(std::bitset<4>{i}.to_string());

                m_application_log.append(": ");
                indicators::k_anonymize(r[i], m_application_log);
                m_application_log.append("\n");
            }
        }

        m_application_log.append("\n");

        {
            auto const r = m_h_unique_tiles_per_user_with_presence.finish();
            for (std::size_t subperiod = 0; subperiod < num_subperiods; ++subperiod) {
                m_application_log.append("H histogram of number of unique tiles per user (with presence > 0) for subperiod ");
                m_application_log.append(std::to_string(subperiod));
                m_application_log.append(":\n");
                r[subperiod].iterate(formatHistogram("\t"));
            }
        }

        m_application_log.append("\n");

        {
            struct Data {
                char const * what;
                indicators::spatial_distribution::HistogramOfWeightValues & hist;
            };
            for (auto const data : {Data{"H", m_h_weight_values}, Data{"Old S", m_s_old_weight_values}}) {
                auto const r = data.hist.finish();
                for (std::size_t subperiod = 0; subperiod < num_subperiods;
                     ++subperiod) {
                    m_application_log.append(data.what);
                    m_application_log.append(" histogram of weight values in subperiod ");
                    m_application_log.append(std::to_string(subperiod));
                    m_application_log.append(":\n");
                    r[subperiod].iterate(formatHistogram("\t"));
                }
            }
        }

        m_application_log.append("\n");

        {
            auto const r = m_average_distances.finish();
            for (std::size_t subperiod = 0; subperiod < num_subperiods;
                 ++subperiod) {
                m_application_log.append(
                        "Histogram of distance between user H and old S average position in subperiod ");
                m_application_log.append(std::to_string(subperiod));
                m_application_log.append(":\n");
                r[subperiod].iterate(formatHistogram("\t"));
            }
        }

        // This one is inside of the next section, but kept here commented out
        // for reference.
        // m_application_log.append("\n");

        {
            using Histogram = indicators::spatial_distribution::BoundingBoxMeasure::Histogram;
            using Result = indicators::spatial_distribution::BoundingBoxMeasure::Result;
            struct Data {
                char const * what;
                Histogram Result::* hist;
            };
            auto const r = m_bounding_box_measure.finish();
            for (auto const data : {Data{"Histogram of user tiles bounding box diagonal length in H", &Result::h_diagonal_length_histogram},
                                    Data{"Histogram of user tiles bounding box diagonal length in old S", &Result::old_s_diagonal_length_histogram},
                                    Data{"Histogram of user tiles bounding box diagonal length difference between old S and new S", &Result::old_s_vs_new_s_diagonal_length_histogram}}) {
                m_application_log.append("\n");
                for (std::size_t subperiod = 0; subperiod < num_subperiods;
                     ++subperiod) {
                    m_application_log.append(data.what);
                    m_application_log.append(" in subperiod ");
                    m_application_log.append(std::to_string(subperiod));
                    m_application_log.append(":\n");
                    (r[subperiod].*(data.hist)).iterate(formatHistogram("\t"));
                }
            }
        }
    }

private: /* Fields: */
    // 6.4.2
    std::uint64_t m_number_of_duplicate_H_records = 0;

    // 6.4.3
    indicators::Count m_h_count = {};
    indicators::Count m_s_old_count = {};
    indicators::Count m_s_new_count = {};

    // 6.4.4
    indicators::SpatiotemporalDistribution m_spatiotemporal_distribution;

    // 6.4.5
    indicators::spatial_distribution::HHistogramCountOfUniqueTilesPerUserWithPresence m_h_unique_tiles_per_user_with_presence;
    indicators::spatial_distribution::HistogramOfWeightValues m_h_weight_values;
    indicators::spatial_distribution::HistogramOfWeightValues m_s_old_weight_values;
    indicators::spatial_distribution::HistogramOfAverageDistances m_average_distances;
    indicators::spatial_distribution::BoundingBoxMeasure m_bounding_box_measure;

    Log & m_application_log;
};

/**
   A class to count processed records, its output is be used for data
   generation tweaks and performance evaluations. Its logic is partly
   overlapping with the Count indicators, but for the sake of completeness of
   for this particular piece of functionality, the logic is also "duplicated"
   here (so the output is uniform, and no weird cross-referencing with the
   indicators is required).
  */
struct DebugRecordCounting {
#if !defined(NDEBUG) || defined(EDEBUG)
    void h() noexcept { ++m_h; }
    void s_old() noexcept { ++m_s_old; }
    void s_new() noexcept { ++m_s_new; }
    void y() noexcept { ++m_y; }

    ~DebugRecordCounting() {
        enclave_printf_log("NUM_H_RECORDS %zu", m_h);
        enclave_printf_log("NUM_S_OLD_RECORDS %zu", m_s_old);
        enclave_printf_log("NUM_S_NEW_RECORDS %zu", m_s_new);
        enclave_printf_log("NUM_Y_RECORDS %zu", m_y);
        try {
            std::uint64_t i = 0;
            for (; i < 10000000; ++i) {
                try {
                    sharemind_hi::enclave::File records_count_file{
                            "records_count" + std::to_string(i),
                            sharemind_hi::FileOpenMode::FILE_OPEN_READ_ONLY};
                    // File already exists, so continue.
                } catch (...) {
                    // File does not exist, so we claim it.
                    break;
                }
            }

            // Just write to `$PWD`, no problem for the dev builds where this
            // data is produced.
            sharemind_hi::enclave::File records_count_file{
                    "records_count" + std::to_string(i),
                    sharemind_hi::FileOpenMode::FILE_OPEN_WRITE_ONLY};
            std::string data =
                    "Record count: (H: " + std::to_string(m_h) + ", S: "
                    + std::to_string(m_s_old) + " -> " + std::to_string(m_s_new)
                    + ", Y: " + std::to_string(m_y) + ", S abs increase: "
                    + std::to_string(m_s_new - m_s_old) + ", S rel increase: "
                    + std::to_string((100.0 * (m_s_new - m_s_old)) / m_s_old)
                    + "\%, Y / S: " + std::to_string(double(m_y) / m_s_new)
                    + ")";
            records_count_file.write(data.data(), data.size());
        } catch (...) {
            enclave_printf_log("Failed to write the debug records count file");
            /* Ignore - it's just a debug file. */
        }
    }

private:
    std::size_t m_h = 0;
    std::size_t m_s_old = 0;
    std::size_t m_s_new = 0;
    std::size_t m_y = 0;
#else
    // Make sure that in Release builds these operations are no-ops.
    void h() noexcept {}
    void s_old() noexcept {}
    void s_new() noexcept {}
    void y() noexcept {}
#endif
};

namespace module_c {
class SingleHumanAnalysis {
public: /* Methods: */
    SingleHumanAnalysis(Statistics & statistics) noexcept
        : m_statistics(statistics)
    {}

    void operator()(std::vector<S> footprints, std::vector<QuantisedFootprint> & result)
    {
        footprints.erase(std::remove_if(RANGE(footprints),
                                        [](S const & e) {
                                            return e.i_column[0]
                                                   < day_quantisation_threshold;
                                        }),
                         footprints.end());

        if (footprints.empty()) {
            m_statistics.highly_nomadic_users += 1;
            return;
        }

        // Now, we want to use random data to sort, but we need to store it
        // somewhere. The python implementation uses `random()` the lambda
        // supplied to the `key` argument which is cached, but in C++ we don't
        // have such a thing. So instead, we overwrite the user id data in the
        // original S structs to contain random data, which is used as a tie
        // braker and to introduce non-determinism.
        auto const id_backup = footprints.front().key.id;
        for (auto & footprint : footprints) {
            std::array<uint64_t, 2> tmp_random{m_rng(), m_rng()};
            static_assert(sizeof(tmp_random) >= sizeof(footprint.key.id), "");
            memcpy(footprint.key.id.data(),
                   tmp_random.data(),
                   sizeof(footprint.key.id));
        }

        // Sort Y_m according to the L_m rules, so we get the ranks and store
        // them inline in Y_m.
        std::sort(RANGE(footprints),
                  CMP_LAMBDA(>, S,
                             e.i_column[0],
                             // XXX Use nested std::max(.,.) to preserve the
                             // lvalue references, instead of std::max({...})
                             // which creates new values.
                             std::max(std::max(e.i_column[1], e.i_column[2]),
                                      e.i_column[3]),
                             e.i_column[1],
                             // XXX This was replaced with random bits, see note above.
                             e.key.id));

        result.reserve(footprints.size());
        for (std::size_t i = 0; i < footprints.size(); ++i) {
            result.emplace_back();
            auto & q = result.back();
            q.key.id = id_backup;
            q.key.tile = footprints[i].key.tile;
            q.rank = i + QuantisedFootprint::FirstRank;

            auto const fvalues = footprints[i].i_column;
            q.values[0] = true;
            for (std::size_t j = 1; j < 4; ++j) {
                q.values[j] = (fvalues[j] / fvalues[0])
                              >= sub_period_quantisation_threshold;
            }
        }
        return;
    }

private: /* Fields: */
    Statistics & m_statistics;
    /** A weak RNG just used for tie breaking */
    Xoshiro256Plus m_rng;
};
} // namespace module_c

namespace module_d {

bool is_inside(TileIndex const & tile_index, ReferenceAreas::value_type const & reference_area) {
    return reference_area.cend() != reference_area.find(tile_index);
}

struct ConnectionStrengths {
private: /* Types: */
    struct ConnectionStrengthHasher {
        std::size_t operator()(ConnectionStrengthKey const & key) const noexcept
        {
            uint64_t key_bytes = {};
            static_assert(sizeof(key) < sizeof(key_bytes), "");
            memcpy(&key_bytes, &key, sizeof(key));
            return std::hash<decltype(key_bytes)>{}(key_bytes);
        }
    };

    struct ConnectionOperand {
        /**
           The number of users that have both tile j and RA r in their usual
           environment.
         */
        double numerator = {};
        /**
           The number of users that have tile j in their usual environment.
         */
        double denominator = {};
    };

    using ConnectionOperands = std::unordered_map<ConnectionStrengthKey,
                                                  ConnectionOperand,
                                                  ConnectionStrengthHasher>;

public: /* Methods: */
    void operator()(Y const & e)
    {
        for (ReferenceAreaIndex ra_index = 0; ra_index < m_reference_areas.size();
             ++ra_index) {
            // Skip this tile if it is in the reference areas (yes, only look
            // at elements outside).
            if (is_inside(e.key.tile, m_reference_areas[ra_index])) { continue; }

            auto & connection_operand =
                    connection_operands[{ra_index, e.key.tile}];
            // e.calibration_weight is 1.0 if calibration is disabled.
            connection_operand.numerator +=
                    static_cast<double>(e.reference_area_indices.test(ra_index))
                    * e.calibration_weight;
            connection_operand.denominator += e.calibration_weight;
        }
    }

    ConnectionStrengths(sharemind_hi::enclave::TaskOutputs & outputs,
                        ReferenceAreas const & reference_areas) noexcept
        : m_outputs(outputs), m_reference_areas(reference_areas)
    {}

    ConnectionStrengths(ConnectionStrengths &&) noexcept = default;

    ~ConnectionStrengths()
    {
        if (connection_operands.empty()) {
            // This is the moved-from instance. We can be rather sure that there
            // is always input data, and hence there is always some data in
            // this map.
            return;
        }

        std::vector<FunctionalUrbanFingerprintReport> result;
        result.reserve(connection_operands.size());

        for (auto const & kv : connection_operands) {
            auto const strength = kv.second.numerator / kv.second.denominator;
            // Applying SDC. Don't add 0 connection strengths to the result.
            if (kv.second.numerator >= sdc_threshold && strength > 1e-20) {
                result.push_back({kv.first, strength});
            }
        }

        m_outputs.put(output_names::functional_urban_fingerprint_report, result);
    }

public: /* Fields: */
    sharemind_hi::enclave::TaskOutputs & m_outputs;
    ReferenceAreas const & m_reference_areas;

private:
    ConnectionOperands connection_operands;
};

std::unordered_map<TileIndex, double, TileIndexHasher>
build_calibration_weights_map(Statistics & statistics,
                              CensusResidents const & residents,
                              TopAnchorDistribution const & top_anchor_dist,
                              bool const with_calibration)
{
    std::unordered_map<TileIndex, double, TileIndexHasher> result;

    if (!with_calibration) { return result; }

    for (auto const & kv : top_anchor_dist) {
        double const anchor_count = kv.second;
        double const resident_count = [&] {
            auto const it = residents.find(kv.first);
            if (it == residents.cend()) {
                return 0.0;
            } else {
                return it->second;
            }
        }();
        auto const max_count = std::max(resident_count, anchor_count);
        assert(anchor_count > 0);
        auto const ratio = resident_count / anchor_count;
        auto const weight = [&]() noexcept -> double {
            if (max_count < 10) {
                return 1;
            } else if (max_count >= 10 && ratio <= 0.2) {
                return 0.2;
            } else if (max_count >= 10 && ratio >= 10) {
                return 10;
            } else {
                return ratio;
            }
        }();
        result.insert(std::make_pair(kv.first, weight));

        // The python code has this calculation in a separate loop. I merged the
        // two loops into one.
        statistics.observed_total_users += anchor_count;
        statistics.adjusted_total_users += weight * anchor_count;
    }
    return result;
}
}
} // namespace

void run(HFileSource h_file,
         SFileSource s_file_in,
         SFileSink s_file_out,
         PseudonymisationKeyRef pseudonymisation_key,
         Perform const what_to_do,
         ReferenceAreas const & reference_areas,
         CensusResidents const & residents,
         bool const with_calibration,
         sharemind_hi::enclave::TaskOutputs & outputs,
         Log & application_log)
{
    // This function is awfully long, because the Stream API creates big, nested
    // types out of the combinators. This means, without C++14 auto function
    // return type deduction support this cannot really be split into nice
    // functions - only lambdas which further increases the nesting level.
    // Instead, this is one big function with some combinator body logic split
    // into their own class where the code is rather long or RAII is used.

    // Writes to application_log in the dtor.
    auto indicators = Indicators{application_log};

    auto debug_record_counting = DebugRecordCounting{};

    /************
     * Module B
     ************/

    /** In an ideal situation, pseudonyms are sorted. This means, we only need
     * to decrypt the first one and just can lookup the following records. */
    struct LastSeen {
        PseudonymisedUserIdentifier pseud_id;
        UserIdentifier id;

        LastSeen(HFileSource & h_file, PseudonymisationKeyRef pseudonymisation_key)
        {
            PseudonymisedUserFootprintUpdates first;
            if (h_file.peek(first)) {
                pseud_id = first.id;
                id = decrypt_pseudonym(pseudonymisation_key, pseud_id);
            } else {
                // The H file is empty, so no records will be decrypted.
                // The members can stay uninitialized.
            }
        }
    } last_seen = {h_file, pseudonymisation_key};
    
    auto sorted_h_file = std::move(h_file)
            //
            >>=
            smap([&](PseudonymisedUserFootprintUpdates const & e) {
                PseudonymisedUserIdentifier pseud_id = e.id;
                if (pseud_id != last_seen.pseud_id) {
                    last_seen.pseud_id = pseud_id;
                    last_seen.id = decrypt_pseudonym(pseudonymisation_key, pseud_id);
                }
                return H{{last_seen.id, e.tile}, e.i_column};
            })
            //
            >>= sort(CMP_LAMBDA(<, H, e.key), detail::mebibytes(64));

    auto cleaned_deduped_sorted_h_file = std::move(sorted_h_file)
            //
            >>= filter([](H const & e) noexcept {
                    bool positive_found = false;
                    for (auto const value : e.i_column) {
                        if (!std::isfinite(value)) { return false; }
                        if (value < 0) { return false; }
                        if (value > 0) { positive_found = true; }
                    }
                    return positive_found;
                })

            // Not using `squash` here, as in theory only one record per tile
            // per user should be in the input data.
            >>= groupBy(CMP_LAMBDA(==, H, e.key))
            //
            >>= flatMap(
                    [&indicators](std::vector<H> const & v, std::vector<H> & result_vec) -> void {
                        // Note: if the input H would be sanitized, `v` would
                        // always only contain a single element.

                        assert(!v.empty());
                        result_vec.push_back(v.front());
                        if (v.size() == 1) { return; }

                        indicators.report_additional_H_duplicates(v.size() - 1);

                        H & result = result_vec.front();
                        for (auto const & e : v) {
                            for (std::size_t i = 0;
                                 i < result.i_column.size();
                                 ++i) {
                                auto const a = result.i_column[i];
                                auto const b = e.i_column[i];
                                result.i_column[i] = std::max(a, b);
                            }
                        }
                        return;
                    });

    // At this point, H values are sorted by (ID, tile_index).
    // Each (ID, tile_index) is unique.
    // There is the invariant that the same holds for S, since the result of
    // the following merge function is also sorted by (ID, tile_index) with
    // (ID, tile_index) being unique.

    auto updated_s = outerJoin(
            std::move(cleaned_deduped_sorted_h_file),
            std::move(s_file_in),
            [](H const & e) /* value */ { return e.key; },
            [](S const & e) /* value */ { return e.key; })
            //
            >>=
            smap([&indicators, &debug_record_counting](
                         std::pair<std::vector<H>, std::vector<S>> const & vv) noexcept
                 -> S {
                S result;
                assert(vv.second.size() <= 1);
                assert(vv.first.size() <= 1);
                if (vv.first.empty()) {
                    indicators.process_s_old_record(vv.second.front());
                    debug_record_counting.s_old();
                    result = vv.second.front();
                } else if (vv.second.empty()) {
                    indicators.process_h_record(vv.first.front());
                    debug_record_counting.h();
                    result = S{vv.first.front().key, vv.first.front().i_column};
                } else {
                    indicators.process_h_record(vv.first.front());
                    indicators.process_s_old_record(vv.second.front());
                    debug_record_counting.h();
                    debug_record_counting.s_old();
                    result = vv.second.front();
                    for (std::size_t i = 0; i < result.i_column.size(); ++i) {
                        result.i_column[i] += vv.first.front().i_column[i];
                    }
                }
                indicators.process_s_new_record(result);
                debug_record_counting.s_new();
                return result;
            });

    if (what_to_do == Perform::OnlyStateUpdate) {
        // The "update S" pipeline is built, just write it to the file ..
        std::move(updated_s) >>= std::move(s_file_out);
        // .. and call it a day.
        return;
    }

    assert(what_to_do == Perform::FullAnalysis);

    // If the full analysis can be done, we don't need to write S back - the NSI
    // request has been fulfilled and related state will be dismissed afterwards.

    Statistics statistics = {};
    TopAnchorDistribution top_anchor_dist;
    // Its somewhere in the range of 600K. To prevent an additional allocation
    // if it is a bit above 600K, just add a buffer.
    top_anchor_dist.reserve(700000);

    auto single_human_analysis = module_c::SingleHumanAnalysis{statistics};

    auto materialized_y = std::move(updated_s)

            // Group by the user id, i.e. put all tiles for the same user into
            // a single group.
            >>= groupBy(CMP_LAMBDA(==, S, e.key.id))
            //
            >>= flatMap([&](std::vector<S> const & footprints,
                           std::vector<QuantisedFootprint> & result) {
            /************
             * Module C
             ************/

                    single_human_analysis(footprints, result);

            /************
             * Module D
             ************/

            /*********************
             * Add Reference Areas
             *********************/

                    // Intermediate storage for the reference area indices for
                    // this user.
                    decltype(Y::reference_area_indices) group_ra_indices{};
                    for (std::size_t i = 0; i < reference_areas.size(); ++i) {
                        for (auto const & q : result) {
                            if (module_d::is_inside(q.key.tile, reference_areas[i])) {
                                group_ra_indices.set(i);
                                // Stop searching for more certificates for this RA,
                                // go to the next RA and start iteration over all
                                // quantised footprints.
                                break;
                            }
                        }
                    }

                    // The result needs to be written to all elements in the group.
                    for (auto & q : result) {
                        q.reference_area_indices = group_ra_indices;
                    }
                    return;
                })

            /***********************************
             * Calculate Top Anchor Distribution
             ***********************************/

            >>= inspect([&](QuantisedFootprint const & e) mutable {
                    // Only keep the 1st ranked tile.
                    if (e.rank == QuantisedFootprint::FirstRank) {
                        ++top_anchor_dist[e.key.tile];
                    }

                    debug_record_counting.y();
                })

            // At this point we need to move fully through `Y` so the
            // TopAnchorDistribution will be filled to build the calibration
            // weights map.
            >>= temporaryOutput();

    auto const weights = module_d::build_calibration_weights_map(
            statistics, residents, top_anchor_dist, with_calibration);

    double group_calibration_weight = 0;

    auto sorted_y = temporarySource<Y>(std::move(materialized_y))

            /*************************
             * Add calibration weights
             *************************/

            >>=
            smap([group_calibration_weight, &weights, with_calibration](Y e) mutable {
                if (! with_calibration) {
                    // Make it the neutral element for multiplication `*`
                    // where it will be used in future invocations.
                    e.calibration_weight = 1.0;
                    return e;
                }

                // The first element in a group (of same user id)
                // has the FirstRank, and no other elements in this
                // group have this rank (all increasing). Hence,
                // when we find this tile, we lookup the weight,
                // cache it and reuse it for the rest of the group.
                if (e.rank == Y::FirstRank) {
                    group_calibration_weight = [&] {
                        auto const it = weights.find(e.key.tile);
                        if (it == weights.cend()) {
                            return 0.0;
                        } else {
                            return it->second;
                        }
                    }();
                }
                e.calibration_weight = group_calibration_weight;
                return e;
            })

            /**********************
             * Connection Strengths
             **********************/

            >>= inspect(module_d::ConnectionStrengths{outputs, reference_areas})

            /****************
             * Sum footprints
             ****************/

            // First sort. Place the result in a temporary variable to highlight
            // that this step materializes data on the disk. But conceptually
            // the following `squash` is tightly coupled to this `sort`.
            >>= sort(CMP_LAMBDA(<, Y, e.key.tile), mebibytes(64));

    std::move(sorted_y)

            // Sum footprints (continued)

            // Optimized squash = groupBy + flatMap required here, because
            // there might be a lot of records for the same tile id which
            // exceeds the available memory.
            >>= squash(
                    CMP_LAMBDA(==, Y, e.key.tile),
                    [](Y const & e) {
                        // Only initialize the data that needs to be
                        // initialized once. The squash function is called
                        // directly afterwards with the same `result` and `e`,
                        // again to do the iterative logic.
                        TotalFootprint result{};
                        result.tile_index = e.key.tile;
                        return result;
                    },
                    [](TotalFootprint & result, Y const & e) {
                        for (std::size_t i = 0; i < result.values.size(); ++i) {
                            result.values[i] +=
                                    // e.calibration_weight is 1.0 if
                                    // calibration is disabled.
                                    e.calibration_weight
                                    * static_cast<double>(e.values[i]);
                        }
                    })

            /************************
             * Total footprint report
             ************************/
            >>= smap([](TotalFootprint result) noexcept -> FingerprintReport {
                    // Applying SDC
                    for (auto & v : result.values) {
                        if (v < sdc_threshold) { v = 0; }
                    }

                    return {result.tile_index, result.values};
                })
            //
            >>= encryptedOutput(outputs, output_names::fingerprint_report);

    /********************************
     * Top anchor distribution report
     ********************************/

    {
        std::vector<TopAnchorDistributionReport> result;
        result.reserve(top_anchor_dist.size());
        for (auto const & p : top_anchor_dist) {
            // Applying SDC
            if (p.second >= sdc_threshold) {
                result.push_back({p.first, p.second});
            }
        }

        outputs.put(output_names::top_anchor_distribution_report, result);
    }

    /*******************
     * Statistics report
     *******************/

    outputs.put(output_names::statistics, &statistics, sizeof(statistics));
}

} // namespace full_analysis
} // namespace enclave
} // namespace eurostat
