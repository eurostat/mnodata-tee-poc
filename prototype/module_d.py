#
# Copyright 2021 European Union
#
# Licensed under the EUPL, Version 1.2 or – as soon they will be approved by 
# the European Commission - subsequent versions of the EUPL (the "Licence");
# You may not use this work except in compliance with the Licence.
# You may obtain a copy of the Licence at:
#
# https://joinup.ec.europa.eu/software/page/eupl
#
# Unless required by applicable law or agreed to in writing, software 
# distributed under the Licence is distributed on an "AS IS" basis,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the Licence for the specific language governing permissions and 
# limitations under the Licence.
#

from copy import deepcopy
from typing import Dict, Generator, List, Tuple

from entities import *
# The parameters might be changed with command line arguments, don't
# use the `from parameters import ...` form.
import parameters


# Calculates total footprint over all users (D matrix in the document)
def sum_footprints(quantised: List[QuantisedFootprint]) -> List[TotalFootprint]:
    totals: List[TotalFootprint] = []

    # Use sorting to group rows by combination of (sub-period, easting,
    # northing). The for loop will count each group.
    quantised.sort(key = lambda x: (x.tile_e, x.tile_n))
    last_key = None

    for row in quantised:
        key = (row.tile_e, row.tile_n)

        if key == last_key:
            totals[-1].totals = \
                [x + y for (x, y) in \
                 zip(totals[-1].totals, row.values)]
        else:
            totals.append(TotalFootprint(
                row.tile_e,
                row.tile_n,
                row.values
            ))

        last_key = key

    return totals

# Applies statistical disclosure control to total footprint (D' matrix in the
# document)
def total_footprint_sdc(total: List[TotalFootprint]) -> List[TotalFootprint]:
    filtered = []

    for row in total:
        x = deepcopy(row)
        x.totals = [0 if x < parameters.sdc_threshold else x for x in x.totals]
        filtered.append(x)

    return filtered

# For each tile count the number of users for whom it was their top anchor tile
# (P matrix in the document)
def calculate_top_anchor_dist(quantised: List[QuantisedFootprint]) \
    -> TopAnchorDistribution:
    top_anchor_dist: TopAnchorDistribution = {}

    for row in quantised:
        if row.rank != 0:
            continue

        key = (row.tile_e, row.tile_n)

        if not key in top_anchor_dist:
            top_anchor_dist[key] = 1
        else:
            top_anchor_dist[key] += 1

    return top_anchor_dist

# Applies statistical disclosure control to top anchors (P' matrix in the
# document)
def sdc_filter_top_anchor_dist(top_anchor_dist: TopAnchorDistribution) \
    -> TopAnchorDistribution:
    filtered: TopAnchorDistribution = {}

    for coords, count in top_anchor_dist.items():
        if count >= parameters.sdc_threshold:
            filtered[coords] = count

    return filtered

# Add list of reference areas that intersect with the user footprint to each row
# of quantised user footprint.
#
# ref_areas format: for each reference area the list of tile coordinates in that
# area.
def add_reference_areas(
    footprints: List[QuantisedFootprint],
    ref_areas: List[List[Coord]]) \
    -> None:

    if len(footprints) == 0:
        return

    # Group by user. NB! Assumes footprints are sorted by user id.
    groups = []
    user = None

    for row in footprints:
        if row.id != user:
            user = row.id
            groups.append([row])
        else:
            groups[-1].append(row)

    for group in groups:
        # Calculate user_ref_areas for each group
        user_ref_areas = []
        user_tiles = set([(row.tile_e, row.tile_n) for row in group])

        for i, area in enumerate(ref_areas):
            # Check if user area intersects with reference area
            if len(set(area) & user_tiles) > 0:
                user_ref_areas.append(i)

        # Add user_ref_areas to each row in group
        for row in group:
            row.ref_areas = user_ref_areas

# Calculate connection strengths to reference areas
#
# NB! Expects that the footprints have been sorted by tile coordinates.
def connection_strength(
    footprints: List[QuantisedFootprint],
    ref_areas: List[List[int]]) \
    -> ConnectionStrengths:

    # Map (tile coordinates, reference area index) to connection strength
    # numerator and denominator. If the key is (t, r) then the numerator is the
    # number of users that have both tile t and RA r in their usual environment
    # and the denominator is the number of users that have tile t in their usual
    # environment. This data structure will be kept as a map (not a stream) in
    # the real implementation because it fits in memory.
    con_operands: Dict[Tuple[Coord, int], Tuple[int, int]] = {}

    for row in footprints:
        coord = (row.tile_e, row.tile_n)
        for area_idx, area_tiles in enumerate(ref_areas):
            # Skip this tile if it's in the reference area
            if coord in area_tiles:
                continue

            key = (coord, area_idx)
            in_ra = 1 if area_idx in row.ref_areas else 0
            if key not in con_operands:
                con_operands[key] = (in_ra, 1)
            else:
                old = con_operands[key]
                new = (old[0] + in_ra, old[1] + 1)
                con_operands[key] = new

    con_strengths: ConnectionStrengths = {}

    for key, val in con_operands.items():
        strength = float(val[0]) / float(val[1])
        # Apply SDC and don't add 0 connection strengths to the result
        if val[0] >= parameters.sdc_threshold and strength > 1e-20:
            con_strengths[key] = strength

    return con_strengths

def add_calibration_weights(
    footprints: List[QuantisedFootprint],
    top_anchor_dist: TopAnchorDistribution,
    residents: Residents) -> None:

    # Calculate mapping from tile to weight
    weights = {}
    for coord, anchor_count in top_anchor_dist.items():
        resident_count = residents[coord]
        weight = None
        max_count = max(resident_count, anchor_count)
        ratio = resident_count / anchor_count
        if max_count < 10:
            weight = 1.0
        elif max_count >= 10 and ratio <= 1 / 5:
            weight = 1 / 5
        elif max_count >= 10 and ratio >= 10:
            weight = 10.0
        else: # 1/5 < ratio < 10
            weight = ratio
        weights[coord] = weight

    # Calculate total number of users before and after adjustment
    observed = 0.0
    adjusted = 0.0
    for coord, count in top_anchor_dist.items():
        observed += count
        adjusted += weights[coord] * count
    print('Observed number of users: ' + str(observed))
    print('Adjusted number of users: ' + str(adjusted))

    # Add weight to each footprint row. The rows are grouped by user id because
    # each row of a user has the same weight.
    groups: List[List[QuantisedFootprint]] = []
    for row in footprints:
        if len(groups) == 0 or row.id != groups[-1][0].id:
            groups.append([row])
        else:
            groups[-1].append(row)

    # NB! The rows of a user are in the top anchor (L_m) ordering after module C
    # added ranks. Due to this we calculate the weight from the first row of the
    # user (the top anchor) and set the same weight for all other rows of the
    # user.
    for group in groups:
        top_anchor = group[0]
        weight = weights[(top_anchor.tile_e, top_anchor.tile_n)]
        for row in group:
            row.weight = weight

# Calculates calibrated total footprint over all users (calibrated D matrix in
# the document)
def sum_footprints_calibrated(quantised: List[QuantisedFootprint]) \
    -> List[TotalFootprint]:

    totals: List[TotalFootprint] = []

    # Use sorting to group rows by combination of (sub-period, easting,
    # northing). The for loop will count each group.
    quantised.sort(key = lambda x: (x.tile_e, x.tile_n))
    last_key = None

    for row in quantised:
        key = (row.tile_e, row.tile_n)

        if key == last_key:
            totals[-1].totals = \
                [x + y * row.weight for (x, y) in \
                 zip(totals[-1].totals, row.values)]
        else:
            totals.append(TotalFootprint(
                row.tile_e,
                row.tile_n,
                [x * row.weight for x in row.values]
            ))

        last_key = key

    return totals

# Calculate connection strengths to reference areas
#
# NB! Expects that the footprints have been sorted by tile coordinates.
def connection_strength_calibrated(
    footprints: List[QuantisedFootprint],
    ref_areas: List[List[int]]) \
    -> ConnectionStrengths:

    # Map (tile coordinates, reference area index) to connection strength
    # numerator and denominator. If the key is (t, r) then the numerator is the
    # number of users that have both tile t and RA r in their usual environment
    # and the denominator is the number of users that have tile t in their usual
    # environment. This data structure will be kept as a map (not a stream) in
    # the real implementation because it fits in memory.
    con_operands: Dict[Tuple[Coord, int], Tuple[float, float]] = {}

    for row in footprints:
        coord = (row.tile_e, row.tile_n)
        weight = row.weight

        for area_idx, area_tiles in enumerate(ref_areas):
            # Skip this tile if it's in the reference area
            if coord in area_tiles:
                continue

            key = (coord, area_idx)
            in_ra = 1 if area_idx in row.ref_areas else 0
            if key not in con_operands:
                con_operands[key] = (in_ra * weight, weight)
            else:
                old = con_operands[key]
                new = (old[0] + in_ra * weight, old[1] + weight)
                con_operands[key] = new

    con_strengths: ConnectionStrengths = {}

    for key, val in con_operands.items():
        strength = float(val[0]) / float(val[1])
        # Apply SDC and don't add 0 connection strengths to the result
        if val[0] >= parameters.sdc_threshold and strength > 1e-20:
            con_strengths[key] = strength

    return con_strengths
