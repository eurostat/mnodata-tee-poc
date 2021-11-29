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

#include "../pseudonymisation_key_enclave/Entities.h"
#include "Comparison.h"
#include "Parameters.h"
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <sharemind-hi/common/ZeroingArray.h>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace eurostat {
namespace enclave {


////////////////////////
//      HELPERS       //
////////////////////////


struct TileIndex {
    uint16_t easting;
    uint16_t northing;
};

CMP_FUN(<, TileIndex, e.easting, e.northing)
CMP_FUN(==, TileIndex, e.easting, e.northing)
CMP_FUN(!=, TileIndex, e.easting, e.northing)

using PseudonymisedUserIdentifier = std::array<uint8_t, aes_block_size>;

/** Technically, subperiod 0 is an aggregation over the other three subperiods. */
constexpr std::size_t num_subperiods = 4;

// For each `i` in the H/S matrix, i.e. this is a full column.
using IColumn = std::array<float, num_subperiods>;

using ReferenceAreaIndex = uint8_t;

struct ConnectionStrengthKey {
    ReferenceAreaIndex reference_area_index;
    TileIndex tile_index;
} __attribute__((packed));

CMP_FUN(==, ConnectionStrengthKey, e.reference_area_index, e.tile_index)

////////////////////////
//       INPUTS       //
////////////////////////


/**
   Section 4.2.3.
   A reference area is a collection of `TileIndex`es and has an `id`. We use a
   flattened structure so we can represent it in a table (csv file). In this
   table, `id`s are grouped and sorted, starting from `0` and without gaps, to
   a maximum of at most `MAX_REFERENCE_AREAS - 1`.
   Examples:
     * a valid sequence: `0, 0, 1, 1, 1, 2, 3`
     * an illegal sequence: `1, 3` (not starting from `0`, `2` is missing).
   Since we use a fixed size NSI report request size (and reference areas are
   uploaded within this NSI report request), this table has a maximum size
   MAX_ELEMENTS_PER_NSI_REPORT_REQUEST
   (`num_reference_areas * average_size_of_reference_area`).
   This is roughly ~2MiB large, so it is fully kept in memory.
 */
struct ReferenceArea {
    using Index = ReferenceAreaIndex;
    Index id;
    TileIndex tile_index;

    // XXX This is used as a template argument to a `std::bitset`. It should
    // better stay somewhere around 100.
    constexpr static std::size_t MAX_REFERENCE_AREAS = 128;
    static_assert(MAX_REFERENCE_AREAS <= std::numeric_limits<ReferenceAreaIndex>::max(), "");

    // In theory it should be a lot less, but we have space for more, so a much
    // larger upper bound is chosen.
    constexpr static std::size_t MAX_ELEMENTS_PER_NSI_REPORT_REQUEST = 1000000;
} __attribute__((packed));
static_assert(sizeof(ReferenceArea) == 5, "");
static_assert((sizeof(ReferenceArea)
               * ReferenceArea::MAX_ELEMENTS_PER_NSI_REPORT_REQUEST)
                              % sizeof(uint64_t)
                      == 0,
              "If this condition fails, then padding would need to be added to the NSI report request which makes construction annoying, hence modify the numbers instead.");

/** Section 4.2.4. For all tiles in the country, how many people live in a tile.
 * So sizeof(uint64_t) x 10^6 */
struct CensusResident {
    TileIndex index;
    double value;

    constexpr static std::size_t MAX_ELEMENTS_PER_NSI_REPORT_REQUEST = 1000000;
} __attribute__((packed));

/**
   (H), Section 4.2.2. Read from an unencrypted file from the disk (from a
   hard-coded path), i.e. sidestepping the `dataUpload` action.
  */
struct PseudonymisedUserFootprintUpdates {
    PseudonymisedUserIdentifier id;
    TileIndex tile;
    /** `values` in the architecture document and reference code. */
    IColumn i_column;
};
static_assert(
        sizeof(PseudonymisedUserFootprintUpdates)
                == sizeof(PseudonymisedUserFootprintUpdates::id)
                           + sizeof(PseudonymisedUserFootprintUpdates::tile)
                           + sizeof(PseudonymisedUserFootprintUpdates::i_column),
        "Padding in struct. Make it packed, reorder or otherwise get rid of it.");


////////////////////////
//      OUTPUTS       //
////////////////////////


/** (D'), Section 4.3.1 */
struct FingerprintReport {
    TileIndex tile_index;
    std::array<double, num_subperiods> values;
} __attribute__((packed));

/** (C), Section 4.3.2 */
struct FunctionalUrbanFingerprintReport {
    ConnectionStrengthKey key;
    double strength;
} __attribute__((packed));

/** (P'), Section 4.3.3 */
struct TopAnchorDistributionReport {
    TileIndex tile_index;
    uint32_t count;
} __attribute__((packed));

/** Section 4.3.4 */
struct Statistics {
    uint32_t highly_nomadic_users;
    uint32_t observed_total_users;
    double adjusted_total_users;
} __attribute__((packed));

////////////////////////
// INTERNAL DATATYPES //
////////////////////////

/** Used internally only. This is the long term pseudonym, truncated sha256 over
 * the actual user's IMSI. */
using UserIdentifier = std::array<uint8_t, hash_bytes>;

struct FootprintKey {
    UserIdentifier id;
    TileIndex tile;
};
static_assert(sizeof(FootprintKey)
                      == sizeof(FootprintKey::id) + sizeof(FootprintKey::tile),
              "Implicit padding: make it packed or solve it otherwise.");

// Use a special memcmp variant for "<": The CMP_FUN alternative turned out to
// be badly optimized by the compiler - at least it appeared at the top of the
// Intel VTune hotspots report. The runtime improves a bit, but less than to be
// expected by the report.
inline bool operator<(FootprintKey const & a, FootprintKey const & b) noexcept {
    return std::memcmp(&a, &b, sizeof(FootprintKey)) < 0;
}
//CMP_FUN(<, FootprintKey, e.id, e.tile)
CMP_FUN(==, FootprintKey, e.id, e.tile)
CMP_FUN(!=, FootprintKey, e.id, e.tile)

/**
   The De-pseudonymised data structure pendant to
   `PseudonymisedUserFootprintUpdates`. Same as `AccumulatedUserFootprint`,
   but to prevent mixing up types, this is a separate one.
  */
struct UserFootprintUpdates {
    FootprintKey key;
    /** `values` in the architecture document and reference code. */
    IColumn i_column;
} /* Only used in-memory, no need to be `packed`. */;
static_assert(sizeof(UserFootprintUpdates) == 32, "");


/** (S), Section 4.4.2. Persistent state, kept in encrypted form.
    Same as `UserFootprintUpdates`, but to prevent mixing up types, this is a
    separate one.
  */
struct AccumulatedUserFootprint {
    FootprintKey key;
    IColumn i_column;
} /* Internal, does not need to be packed. */;
static_assert(sizeof(AccumulatedUserFootprint) == sizeof(FootprintKey) + sizeof(IColumn), "");
static_assert(sizeof(AccumulatedUserFootprint) == 32, "");

/**
    Internal, Y_m from Fabio's document.
    Created in Module b.
 */
struct QuantisedFootprint {
    FootprintKey key;

    std::array<bool, num_subperiods> values = {};

    static constexpr uint32_t FirstRank = 0;
    // Rank in anchor tile (L_m) ordering
    uint32_t rank = 0;

    // Not filled in module C, but in module D `add_reference_areas`.
    // If a bit at position `n` is set, then this person is in reference area `n`.
    std::bitset<ReferenceArea::MAX_REFERENCE_AREAS> reference_area_indices;
    static_assert(ReferenceArea::MAX_REFERENCE_AREAS <= 128, "Are you sure? This is going out of hand ... This struct is going to be huge.");

    // Not filled in module C, but in module D `add_calibration_weights`.
    double calibration_weight = 0;

    static bool idx_cmp(QuantisedFootprint const & left,
                        QuantisedFootprint const & right) noexcept
    {
        CMP(<, e.key);
    }
} /* Internal, does not need to be packed. */;
static_assert(sizeof(QuantisedFootprint) == 48, "");

/** (D), as defined in the python code. Same members as FingerprintReport, but
    trying to prevent type confusion here. Since we don't write it out to a file
    but hold only one or two elements at the same time in memory, it is not
    necessary to have it packed. Makes using the type a bit easier.
 */
struct TotalFootprint {
    TileIndex tile_index;
    std::array<double, num_subperiods> values;
};


struct TileIndexHasher {
    std::size_t operator()(TileIndex const tile_index) const noexcept {
        // Reuse the hashing function available for uint32_t
        uint32_t tile_index_bytes;
        static_assert(sizeof(tile_index_bytes) == sizeof(tile_index), "");
        memcpy(&tile_index_bytes, &tile_index, sizeof(tile_index));
        return std::hash<decltype(tile_index_bytes)>{}(tile_index_bytes);
    }
};

/** Not SDC filtered. */
using TopAnchorDistribution = std::unordered_map<TileIndex, uint32_t, TileIndexHasher>;

// Needs to be built up from the `CensusResident` input.
using CensusResidents =
        std::unordered_map<TileIndex, double, TileIndexHasher>;

// Needs to be built up from the `ReferenceArea` input.
using ReferenceAreas = std::vector<std::unordered_set<TileIndex, TileIndexHasher>>;

// Need to use a C-style array here due to the use of SGX SDK APIs.
using PseudonymisationKeyRef = const uint8_t (&)[PseudonymisationKeyLength];

using Log = std::string;

} // namespace enclave
} // namespace eurostat
