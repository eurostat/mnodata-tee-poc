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

from base64 import b64decode
from typing import List

from entities import Footprint

# Remove invalid rows and merge rows with the same coordinate pair and user
# identifier
def clean_and_remove_duplicates(updates: List[Footprint]) -> List[Footprint]:

    groups: List[List[Footprint]] = []
    last_key = None

    # Sort updates
    updates.sort(key = lambda x: (x.id, x.tile_e, x.tile_n))

    # Group rows by key
    for row in updates:
        #print(f'DBGDBG HHHB_1 ({row.id[0]} {row.id[1]} {row.id[2]}) ({row.tile_e} {row.tile_n}) ({row.values[0]}) {row.values[1]} {row.values[2]} {row.values[3]}')
        # Remove rows that are invalid (have a negative value) or don't actually
        # contain information (no value exceeds 0)
        if any([x < 0 for x in row.values]) or \
            max(row.values) == 0:
            continue

        key = (row.id, row.tile_e, row.tile_n)
        if key == last_key:
            groups[-1].append(row)
        else:
            groups.append([row])
        last_key = key

    # Merge rows in each group: take the maximum value in each sub-period over
    # all the rows in group
    res = []
    for group in groups:
        if len(group) == 1:
            res.append(group[0])
        else:
            first = group[0]
            summary_row = Footprint(first.id, first.tile_e, first.tile_n, [])
            for i in range(4):
                summary_row.values.append(max([row.values[i] for row in group]))
            res.append(summary_row)

    return res

# Updates accumulated footprints. It is assumed that rows in the 'updates'
# parameter are from the same period.
def ingest_period_footprints_into_state(
    footprints: List[Footprint],
    updates: List[Footprint]) \
    -> List[Footprint]:

    # The 'footprints' parameter is the state. This function merges footprint
    # updates with the state. 'footprints' is initially an empty list. It is
    # updated in such a manner that it will always remain sorted by the user
    # identifier.
    #
    # The merging logic in the while loop essentially implements a left-join of
    # 'updates' and 'footprints' by the id column. The joined rows are
    # aggregated by summing the frequencies of tiles. The function is
    # implemented this way because this algorithm is suitable for the
    # privacy-preserving implementation.

    # Remove duplicates. Also sorts updates by user identifier.
    updates = clean_and_remove_duplicates(updates)

    # Merge updates into footprints
    left_idx = 0
    right_idx = 0
    joined = []
    while left_idx != len(updates):
        update = updates[left_idx]

        # Add footprints that were not updated
        while right_idx < len(footprints) and \
                footprints[right_idx].key() < update.key():
            joined.append(footprints[right_idx])
            right_idx += 1

        # Update footprint and add it to output if user id and tile match this
        # update
        if right_idx < len(footprints) and \
                footprints[right_idx].key() == update.key():
            footprint = footprints[right_idx]
            footprint.values = [x + y for (x, y) in
                                zip(footprint.values, update.values)]
            joined.append(footprint)
            right_idx += 1
        # Update does not have matching footprint. Add update as new footprint.
        else:
            joined.append(update)

        left_idx += 1

    # Footprints that are not updated but have a key greater than the last
    # update
    while right_idx != len(footprints):
        joined.append(footprints[right_idx])
        right_idx += 1

    return joined
