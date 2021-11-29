#!/usr/bin/env python3
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

import argparse
import csv
import glob
import os
import re
from typing import Dict, Generator, List, Tuple

from entities import (Footprint, TotalFootprint, Residents, Coord,
                      TopAnchorDistribution)
import parameters
from module_b import ingest_period_footprints_into_state
from module_c import quantise_footprints
from module_d import (total_footprint_sdc, add_reference_areas,
                      calculate_top_anchor_dist, add_calibration_weights,
                      sum_footprints, sdc_filter_top_anchor_dist,
                      connection_strength, sum_footprints_calibrated,
                      connection_strength_calibrated)
from util import write_csv

please_debug = False

def debug(str):
    if please_debug:
        print(str)

# Pretty print tables or dictionaries for debugging
def pretty(x):
    if isinstance(x, List):
        return '\n'.join([str(el) for el in x])
    elif isinstance(x, Dict):
        rows = []
        for key, val in x.items():
            rows.append('{0}: {1}'.format(key, val))
        return '\n'.join(rows)

# Read footprint update data of one period (e.g. day) from CSV file
def read_updates(filename: str) -> List[Footprint]:
    updates = []
    with open(filename, 'r') as f:
        reader = csv.reader(f)
        # skip header
        next(reader)
        for row in reader:
            assert len(row) == 7
            values = [float(row[i]) for i in range(3, 7)]
            updates.append(Footprint(row[0], row[1], row[2], values))
    return updates

def read_residents(filename: str) -> Residents:
    residents = {}
    with open(filename, 'r') as f:
        reader = csv.reader(f)
        # skip header
        next(reader)
        for row in reader:
            assert len(row) == 3
            coords = (int(row[0]), int(row[1]))
            residents[coords] = float(row[2])
    return residents

def read_reference_areas(filename: str) -> List[List[Coord]]:
    ref_areas = []
    last_id = None
    seen_ids = set()
    with open(filename, 'r') as f:
        reader = csv.reader(f)
        # skip header
        next(reader)

        tiles = []
        for row in reader:
            assert len(row) == 3
            id = int(row[0])

            if id != last_id and last_id is not None:
                ref_areas.append(tiles)
                tiles = []

            tile_e = int(row[1])
            tile_n = int(row[2])
            seen_ids.add(id)
            tiles.append((tile_e, tile_n))
            last_id = id

    ref_areas.append(tiles)

    if sorted(list(seen_ids)) != list(range(len(seen_ids))):
        raise Exception('Reference area indices must be sequential')

    return ref_areas

StringGenerator = Generator[List[str], None, None]

# Generator used for serialising TotalFootprint objects when writing CSV
# fingerprint report
def fingerprint_row_generator(footprints: List[TotalFootprint]) \
        -> StringGenerator:
    for footprint in footprints:
        assert(len(footprint.totals) == 4)
        yield [
            footprint.tile_e,
            footprint.tile_n
        ] + footprint.totals

# Write fingerprint report (D) as CSV file
def write_fingerprint_report(footprints: List[TotalFootprint], filename: str) \
        -> None:
    path = os.path.join('reports', filename)
    print("Writing fingerprint report (D') to {0}".format(path))
    write_csv(
        path,
        fingerprint_row_generator(footprints),
        ['tile_e', 'tile_n', 'value_0', 'value_1', 'value_2', 'value_3'])

# Generator used for serialising TopAnchor objects when writing top anchors CSV
# report
def top_anchors_row_generator(top_anchors: TopAnchorDistribution) \
        -> StringGenerator:
    for coords, value in top_anchors.items():
        yield list(map(str, [coords[0], coords[1], value]))

def write_top_anchors_distribution_report(top_anchors: TopAnchorDistribution) \
        -> None:
    path = os.path.join('reports', 'top-anchor-distribution.csv')
    print("Writing top anchors report (P') to {0}".format(path))
    write_csv(
        path,
        top_anchors_row_generator(top_anchors),
        ['tile_e', 'tile_n', 'value']
    )

# Generator used for serialising FUF report rows when writing CSV file
def fuf_row_generator(connection_strengths: Dict[Tuple[Coord, int], float]) \
        -> StringGenerator:
    for key, strength in connection_strengths.items():
        yield list(map(str, [key[1], key[0][0], key[0][1], strength]))

# Write FUF report as CSV file
def write_fuf_report(
        connection_strengths: Dict[Tuple[Coord, int], float],
        filename: str) -> None:
    path = os.path.join('reports', filename)
    print('Writing FUF report to {0}'.format(path))
    write_csv(
        path,
        fuf_row_generator(connection_strengths),
        ['reference_area', 'tile_e', 'tile_n', 'strength'])

def main():
    parser = argparse.ArgumentParser(
        description='ESTAT 2019.0232 prototype')
    parser.add_argument('-i', type=str, required=False, metavar='PATH',
                        help='base path of intra-period footprint files')
    parser.add_argument(
        '--day-quantisation-threshold',
        type=float,
        default=parameters.day_quantisation_threshold)
    parser.add_argument(
        '--sub-period-quantisation-threshold',
        type=float,
        default=parameters.sub_period_quantisation_threshold)
    parser.add_argument(
        '--sdc-threshold',
        type=float,
        default=parameters.sdc_threshold)
    args = parser.parse_args()
    parameters.day_quantisation_threshold = args.day_quantisation_threshold
    parameters.sub_period_quantisation_threshold = args.sub_period_quantisation_threshold
    parameters.sdc_threshold = args.sdc_threshold
    data_path = 'data' if args.i is None else args.i

    # Accumulated footprint state table
    footprints: List[Footprint] = []

    # Module B: ingest footprint updates of each period into footprint state
    for filename in glob.glob(os.path.join(data_path, 'day-*-updates.csv')):
        updates = read_updates(filename)
        footprints = ingest_period_footprints_into_state(footprints, updates)

    debug('Updated footprints')
    debug(pretty(footprints))

    # Module C: transform footprint state into data indicating which tiles are
    # prevalent for each user
    quantised = quantise_footprints(footprints)
    debug('Quantised footprints')
    debug(pretty(quantised))

    # Each reference area is a list of tile coordinates that make up the area
    ref_areas = read_reference_areas(
        os.path.join(data_path, 'reference-areas.csv'))

    # Add a list to each users footprint indicating which reference areas the
    # user visits
    add_reference_areas(quantised, ref_areas)
    debug('Quantised footprints with reference areas')
    debug(pretty(quantised))

    # Calculate top anchors
    top_anchors_dist = calculate_top_anchor_dist(quantised)
    debug('Top anchors distribution')
    debug(pretty(top_anchors_dist))

    # Maps tile coordinates to the number of residents in the tile (\ell)
    residents = read_residents(os.path.join(data_path, 'residents.csv'))

    # Calculate the calibration weight for each user
    add_calibration_weights(quantised, top_anchors_dist, residents)
    debug('Quantised footprints with weights')
    debug(pretty(quantised))

    # Sum quantised footprints of all users to get a table with the number of
    # visitors of tiles
    total = sum_footprints(quantised)
    debug('Total footprint (D)')
    debug(pretty(total))

    # Apply statistical disclosure controls: set the esimated counts of tiles
    # with too few visitors to zero and remove top anchors that don't exceed the
    # threshold
    total_sdc = total_footprint_sdc(total)
    debug("Total footprint after SDC (D')")
    debug(pretty(total_sdc))
    write_fingerprint_report(total_sdc, 'total-footprint.csv')

    top_anchors_dist_sdc = sdc_filter_top_anchor_dist(top_anchors_dist)
    debug("Top anchors after SDC (P')")
    debug(pretty(top_anchors_dist_sdc))
    write_top_anchors_distribution_report(top_anchors_dist_sdc)

    # For each reference area and tile outside of the area calculate the
    # connection strength of the tile to the area
    con_strengths = connection_strength(quantised, ref_areas)
    debug('Connection strengths')
    debug(pretty(con_strengths))
    write_fuf_report(con_strengths, 'functional-urban-fingerprint.csv')

    # Calibrated versions of total footprint (D) and functional urban
    # fingerprint (FUF)

    # Calculate calibrated D report
    calibrated_total = sum_footprints_calibrated(quantised)
    debug('Calibrated total footprint (D)')
    debug(pretty(calibrated_total))

    # Apply statistical disclosure control to calibrated total footprint
    debug("Calibrated total footprint after SDC (D')")
    debug(pretty(calibrated_total))
    calibrated_total_sdc = total_footprint_sdc(calibrated_total)
    write_fingerprint_report(calibrated_total_sdc,
                             'calibrated-total-footprint.csv')

    # Calculate calibrated FUF report
    con_strengths_calibrated = connection_strength_calibrated(
        quantised, ref_areas)
    debug('Calibrated connection strengths')
    debug(pretty(con_strengths_calibrated))
    write_fuf_report(con_strengths_calibrated,
                     'calibrated-functional-urban-fingerprint.csv')

if __name__ == "__main__":
    main()
