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

from random import random
from typing import List, Tuple

from entities import Footprint, QuantisedFootprint
# The parameters might be changed with command line arguments, don't
# use the `from parameters import ...` form.
import parameters


def footprint_sort_key(values):
    return (values[0], max(values[1:4]), values[1], random())

# Calculates summarised footprints (Y matrices in the document) from accumulated
# footprints. Also adds rank number if a tile is a top anchor for the user (tile
# is in L_m in the document terms).
def quantise_footprints(footprints: List[Footprint]) -> List[QuantisedFootprint]:
    # The first loop quantises the footprints and discards footprints whose
    # whole-day value is below day_quantisation_threshold. During this it also
    # groups quantised footprints of each user along with values of the
    # unquantised footprints. The grouping is necessary to find the top anchor
    # tiles of each user. The original values are used for the anchor sorting.
    #
    # Note that this function passes over the data twice (first 'footprints'
    # then 'groups') but in an implementation with streams it would be operating
    # group-by-group.

    quantised = []
    groups: List[List[Tuple[QuantisedFootprint, List[float]]]] = []
    highly_nomadic = 0
    last_id = None
    group_size = 0

    for footprint in footprints:
        # Start of new group
        if footprint.id != last_id:
            # If the previous group was empty then the user was filtered out by
            # the quantisation and is a highly nomadic user
            if group_size == 0:
                highly_nomadic += 1

            last_id = footprint.id
            group_size = 0

        day_value = int(footprint.values[0] >= parameters.day_quantisation_threshold)

        # If the whole-day value did not exceed the threshold, remove the row
        if day_value == 0:
            continue

        values = [1]
        for i in range(1, 4):
            values.append(int(footprint.values[i] / footprint.values[0] >= \
                 parameters.sub_period_quantisation_threshold))

        quantised_footprint = QuantisedFootprint(
            footprint.id,
            footprint.tile_e,
            footprint.tile_n,
            values)

        if len(groups) == 0 or quantised_footprint.id != groups[-1][0][0].id:
            groups.append([(quantised_footprint, footprint.values)])
        else:
            groups[-1].append((quantised_footprint, footprint.values))

        group_size += 1

    if len(footprints) != 0:
        # Account for the last group when counting nomadic users
        if group_size == 0:
            highly_nomadic += 1

        # Account for the highly_nomadic increment caused by last_id changing
        # from None to the first id
        highly_nomadic -= 1

    print('Number of highly nomadic users: ' + str(highly_nomadic))

    for group in groups:
        group.sort(key=lambda x: footprint_sort_key(x[1]), reverse=True)

        for i, x in enumerate(group):
            row = x[0]
            row.rank = i
            quantised.append(row)

    return quantised
