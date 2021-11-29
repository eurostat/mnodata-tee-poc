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
from base64 import b64decode
import datetime as dt
import fileinput
import math
import os
import random
import sys

import geopandas
import numpy as np
import pandas as pd


# Parameters used by the algorithm. These can be overwritten using
# command-line arguments.
USER_COUNT = 1000
DAY_COUNT = 1
SILENT_PERIOD_PROBABILITY = 0
TILES_PER_SUBPERIOD = 4
ANCHOR_CANDIDATE_COUNT = 1
DUPLICATE_COUNT = 0
PEC1 = 0.5
PEC2 = 2
FILL_SUBPERIOD_FROM_NIGHT_TILE_PROBABILITY = 0.4
FILL_SUBPERIOD_FROM_DAY_TILE_PROBABILITY = 0.2
W_FILL_MAX = 1
# Probability of picking normal distribution instead of Pareto when
# generating intra-period footprints
NORMAL_DISTR_PROBABILITY = 0.7
# Population column name in Belgium population GeoJSON file
POP_COL_NAME = 'ms_population_20160101'
REFERENCE_AREA_FILES = ['brussels.csv']
# Have it here so in the TEP we can overwrite it.
INPUT_DIR = 'input'
OUTPUT_DIR = 'output'


def parse_tile_coords(grd_floaid):
    # The software commonly used for creating ETRS89-LAEA5210 grids uses this
    # format for grd_floaid attribute: AAAA.BBBB.C where AAAA is x coordinate in
    # km, BBBB is y coordinate in km and C is country code.
    parts = grd_floaid.split('.')
    assert len(parts) == 3
    x = int(parts[0])
    y = int(parts[1])
    return (x, y)

def read_population_map(filepath, extra_columns):
    df = geopandas.read_file(filepath)
    tile_coords = df.grd_floaid.apply(parse_tile_coords)
    df['tile_e'] = tile_coords.apply(lambda x: x[0])
    df['tile_n'] = tile_coords.apply(lambda x: x[1])
    df['population'] = df[POP_COL_NAME]
    wanted_cols = ['tile_e', 'tile_n'] + extra_columns
    return df[wanted_cols]

def write_residents(geo_data):
    geo_data.to_csv(os.path.join(OUTPUT_DIR, 'residents.csv'),
                    index=False,
                    columns=['tile_e', 'tile_n', 'population'],
                    header=['tile_e', 'tile_n', 'value'])

def add_tile_probabilities(geo_data):
    geo_data['probability'] = geo_data.population.cumsum() / geo_data.population.sum()

def clip(x, low, high):
    return min(high, max(x, low))

class SummedAreaTable:
    def __init__(self, data):
        # Maps tile coordinates (x, y) to total population in rectangle with
        # upper right corner in (x, y)
        self.sums = {}

        # Map tile coordinates to population in tile. Only used while
        # constructing the table.
        population = {}
        self.e_min = 1e10
        self.e_max = -1e10
        self.n_min = 1e10
        self.n_max = -1e10

        for i, row in data.iterrows():
            e = row.tile_e
            n = row.tile_n
            self.e_min = min(self.e_min, e)
            self.e_max = max(self.e_max, e)
            self.n_min = min(self.n_min, n)
            self.n_max = max(self.n_max, n)
            population[(e, n)] = row.population

        self.e_min = int(self.e_min)
        self.e_max = int(self.e_max)
        self.n_min = int(self.n_min)
        self.n_max = int(self.n_max)

        # Note: table is constructed for the bounding box of the country
        for n in range(self.n_min, self.n_max + 1):
            for e in range(self.e_min, self.e_max + 1):
                self.sums[(e, n)] = population.get((e, n), 0) + \
                    self._getc(e, n - 1) + \
                    self._getc(e - 1, n) - \
                    self._getc(e - 1, n - 1)

    # Used when constructing table
    def _getc(self, e, n):
        return self.sums.get((e, n), 0)

    # Get sum of rectangle with top right corner at 'coords' which is an (e, n)
    # tuple
    def _get(self, coords):
        e = clip(coords[0], self.e_min, self.e_max)
        n = clip(coords[1], self.n_min, self.n_max)
        return self.sums[(e, n)]

    def population_around(self, easting, northing, rtile):
        # Rectangle corners are labeled clockwise starting from top left. Area
        # is: SAT(b) - SAT(d) - SAT(a) + SAT(c)
        a = (easting - rtile - 1, northing + rtile)
        b = (easting + rtile, northing + rtile)
        c = (easting - rtile - 1, northing - rtile - 1)
        d = (easting + rtile, northing - rtile - 1)
        return self._get(b) - self._get(d) - self._get(a) + self._get(c)

def find_rtile(sat, easting, northing):
    rtile = 1
    while sat.population_around(easting, northing, rtile) < 5000:
        rtile += 1
    return rtile

def add_rtile(geo_data):
    sat = SummedAreaTable(geo_data)
    rtile = []
    for i, row in geo_data.iterrows():
        rtile.append(find_rtile(sat, row['tile_e'], row['tile_n']))
    geo_data['rtile'] = rtile

def np_roundint(x):
    return np.around(x).astype('int64')

def r_pos_error(rtile):
    r = np.random.rand() * (PEC2 - PEC1) + PEC1
    return np.array(rtile * r)

def generate_user_anchors(geo_data):
    # List of numpy matrices. Each matrix gives e, n coordinates of each user's
    # anchor. Anchor matrices are in sub-period order: 1, 2, 3.
    anchors = []
    # List of numpy arrays. Each array gives r_pos_error of corresponding
    # sub-period anchors.
    errors = []

    # Index where probability[i] >= p
    indices = np.searchsorted(
        geo_data.probability, np.random.rand(USER_COUNT), 'right')
    rows = geo_data.iloc[indices, :]
    night_e = rows['tile_e'] + np.random.rand(USER_COUNT)
    night_n = rows['tile_n'] + np.random.rand(USER_COUNT)
    night_pos = np.vstack([night_e, night_n]).T
    night_rtiles = rows.rtile
    anchors.append(night_pos)
    errors.append(r_pos_error(night_rtiles))

    for sub_period in [2, 3]:
        anchor_candidates = np.empty((USER_COUNT, 2 * ANCHOR_CANDIDATE_COUNT))
        candidate_errors = np.empty((USER_COUNT, ANCHOR_CANDIDATE_COUNT))
        candidate_populations = np.empty((USER_COUNT, ANCHOR_CANDIDATE_COUNT))

        # Generate ANCHOR_CANDIDATE_COUNT anchors for each user
        for candidate_i in range(ANCHOR_CANDIDATE_COUNT):
            # Use a loop to randomly pick an anchor tile because the offset
            # might move it outside the borders of the country. Try again until
            # it lands inside the country.
            needs_anchor = np.arange(USER_COUNT)
            anchors_ = np.empty((USER_COUNT, 2))
            errors_ = np.empty(USER_COUNT)
            population_counts = np.empty(USER_COUNT)

            while len(needs_anchor) > 0:
                night_rtiles_ = night_rtiles.iloc[needs_anchor]
                e = night_e.iloc[needs_anchor] + \
                    np.random.normal(scale=night_rtiles_)
                n = night_n.iloc[needs_anchor] + \
                    np.random.normal(scale=night_rtiles_)
                subset = get_subset(geo_data, e, n, needs_anchor)
                anchors_[subset['good_users'], 0] = subset['e']
                anchors_[subset['good_users'], 1] = subset['n']
                errors_[subset['good_users']] = r_pos_error(subset['rtile'])
                population_counts[subset['good_users']] = subset['population']
                needs_anchor = subset['bad_users']

            col_start = candidate_i * 2
            anchor_candidates[:, col_start:col_start + 2] = anchors_
            candidate_errors[:, candidate_i] = errors_
            candidate_populations[:, candidate_i] = population_counts

        # For each user pick the anchor candidate with the highest population
        col_idx = np.argmax(candidate_populations, axis=1)
        row_idx = np.arange(USER_COUNT)
        errors.append(candidate_errors[row_idx, col_idx])
        e = anchor_candidates[row_idx, col_idx * 2]
        n = anchor_candidates[row_idx, col_idx * 2 + 1]
        anchors.append(np.vstack([e, n]).T)

    # Move sub-period 3 anchor closer to sub-period 2 anchor. The calculated
    # point can be outside the country so this is in a loop again.
    needs_moving = np.arange(USER_COUNT)
    while len(needs_moving) > 0:
        p = np.random.rand(len(needs_moving))

        def move(dim):
            return p * anchors[1][needs_moving, dim] + \
                (1 - p) * anchors[2][needs_moving, dim]

        e = move(0)
        n = move(1)
        subset = get_subset(geo_data, e, n, needs_moving)
        good_users = subset['good_users']
        anchors[2][good_users, 0] = subset['e']
        anchors[2][good_users, 1] = subset['n']
        errors[2][good_users] = r_pos_error(subset['rtile'])
        needs_moving = subset['bad_users']

    return anchors, errors

# For each user with generated (e, n) coordinates find the corresponding row in
# geo_data. Users whose (e, n) is outside the map are in the bad_users array of
# the returned dictionary.
def get_subset(geo_data, e, n, users):
    right = pd.DataFrame.from_dict({
        'user': users,
        'tile_e': np_roundint(e),
        'tile_n': np_roundint(n),
        'float_e': e,
        'float_n': n
    })
    merged = pd.merge(geo_data, right, on=['tile_e', 'tile_n'], how='right')
    bad = np.isnan(merged.rtile)
    good = np.logical_not(bad)

    return {
        'good_users': merged.user[good].array,
        'bad_users': merged.user[bad].array,
        'rtile': merged.rtile[good].array,
        'e': merged.float_e[good].array,
        'n': merged.float_n[good].array,
        'population': merged.population[good].array
    }

# Generates anchors for each user. Here anchor is a "main tile" for a
# sub-period. In each day and each sub-period the user is positioned randomly
# near the corresponding anchor.
def generate_user_data(geo_data):
    regular_anchors, regular_errors = generate_user_anchors(geo_data)
    vacation_anchors, vacation_errors = generate_user_anchors(geo_data)

    max_vacation_length = 3 * 7

    # Workaround in case data is generated only for a few days. By the
    # spec the generator should be used with a large enough --days
    # parameter.
    if DAY_COUNT < max_vacation_length:
        max_vacation_length = math.ceil(DAY_COUNT / 3)

    user_data = []

    for i in range(USER_COUNT):
        user_reg_anchors = []
        user_vac_anchors = []
        user_reg_errors = []
        user_vac_errors = []

        for sub_period in range(0, 3):
            e = regular_anchors[sub_period][i, 0]
            n = regular_anchors[sub_period][i, 1]
            user_reg_anchors.append((e, n))
            user_reg_errors.append(regular_errors[sub_period][i])

            e = vacation_anchors[sub_period][i, 0]
            n = vacation_anchors[sub_period][i, 1]
            user_vac_anchors.append((e, n))
            user_vac_errors.append(vacation_errors[sub_period][i])

        vacation_length = random.randint(1, max_vacation_length + 1)
        vacation_start = random.randint(0, DAY_COUNT - vacation_length + 1)
        vacation_end = vacation_start + vacation_length

        user_data.append({
            'regular_anchors': user_reg_anchors,
            'vacation_anchors': user_vac_anchors,
            'regular_errors': user_reg_errors,
            'vacation_errors': user_vac_errors,
            'vacation_start': vacation_start,
            'vacation_end': vacation_end
        })

    return user_data

def generate_tile_offsets(errors):
    n = errors.size

    condition = np.random.rand(n) < NORMAL_DISTR_PROBABILITY
    r_normal = np.random.normal(scale=errors)
    r_pareto = np.random.pareto(a=1.1, size=n) * 2
    r = np.where(condition, r_normal, r_pareto)

    phi = np.random.rand(n) * 2 * math.pi
    e_offsets = r * np.cos(phi)
    n_offsets = r * np.sin(phi)
    return np.vstack([e_offsets, n_offsets]).T

def ids_to_pseudonyms(ids, stdin, duplicate_no):
    if stdin is None:
        return ids + duplicate_no * USER_COUNT

    pseudonyms = np.empty(USER_COUNT, dtype=object)
    for i in range(USER_COUNT):
        pseudonyms[i] = next(stdin).strip()

    return pseudonyms[ids]

def fill_helper(df, col, filter):
    values = np.random.rand(np.sum(filter)) * W_FILL_MAX
    df.loc[filter, col] = values
    df.loc[filter, 'value_0'] += values

def fill_sub_period_from_tile(df):
    night_not_zero = df['value_1'] > 1e-15
    prob = np.where(night_not_zero,
                    FILL_SUBPERIOD_FROM_NIGHT_TILE_PROBABILITY,
                    FILL_SUBPERIOD_FROM_DAY_TILE_PROBABILITY)
    for sub_period in range(1, 4):
        col = 'value_' + str(sub_period)
        sub_period_is_zero = df[col] < 1e-15
        n = df.shape[0]
        filter = np.logical_and(sub_period_is_zero, np.random.rand(n) < prob)
        fill_helper(df, col, filter)

def generate_footprint_updates_rows(day, user_data, all_coordinates, stdin):
    ids = []
    sub_periods = []
    anchors = []
    errors = []

    for id in range(USER_COUNT):
        this_user_data = user_data[id]
        anchors_ = None
        errors_ = None

        if day >= this_user_data['vacation_start'] and \
           day <= this_user_data['vacation_end']:
            anchors_ = this_user_data['vacation_anchors']
            errors_ = this_user_data['vacation_errors']
        else:
            anchors_ = this_user_data['regular_anchors']
            errors_ = this_user_data['regular_errors']

        for sub_period in range(0, 3):
            if random.random() < SILENT_PERIOD_PROBABILITY:
                continue

            for i in range(TILES_PER_SUBPERIOD):
                ids.append(id)
                sub_periods.append(sub_period + 1)
                anchors.append(anchors_[sub_period])
                errors.append(errors_[sub_period])

    ids = np.array(ids)
    e = np.array([x[0] for x in anchors])
    n = np.array([x[1] for x in anchors])
    anchors = np.vstack([e, n]).T
    errors = np.array(errors)
    row_count = len(ids)
    tiles = np.empty((row_count, 2), dtype='int64')
    needs_fixing = np.arange(row_count)
    values = np.random.rand(row_count)

    while needs_fixing.size > 0:
        offsets = generate_tile_offsets(errors[needs_fixing])
        tiles[needs_fixing] = np_roundint(anchors[needs_fixing] + offsets)

        next_needs_fixing = []
        for i in needs_fixing:
            pair = (tiles[i, 0], tiles[i, 1])
            if pair not in all_coordinates:
                next_needs_fixing.append(i)
        needs_fixing = np.array(next_needs_fixing)

    # There can be multiple rows with key (id, tile_e, tile_n,
    # sub_period). We must aggregate before doing anything else.
    e = tiles[:, 0]
    n = tiles[:, 1]
    df = pd.DataFrame({
        'id': ids,
        'tile_e': e,
        'tile_n': n,
        'sub_period': sub_periods,
        'value': values
    })
    key_cols = ['id', 'tile_e', 'tile_n', 'sub_period']
    df = df.groupby(key_cols).sum().reset_index()

    # Use aggregation to calculate sub-period 0 rows
    day_df = df[['id', 'tile_e', 'tile_n', 'value']]
    day_df = day_df.groupby(['id', 'tile_e', 'tile_n']).sum().reset_index()
    day_df = day_df.rename(columns={'value': 'value_0'})

    # Pivot the dataframe so that sub-period values would become columns
    key_cols = ['id', 'tile_e', 'tile_n', 'sub_period']
    sub_periods_df = df.set_index(key_cols)['value'].unstack()
    sub_periods_df = sub_periods_df.rename_axis(columns=None).reset_index() \
        .rename(columns={
            1: 'value_1',
            2: 'value_2',
            3: 'value_3'
        })
    for i in range(3):
        col = 'value_' + str(i + 1)
        nan_rows = np.isnan(sub_periods_df[col])
        sub_periods_df.loc[nan_rows, col] = 0

    df = day_df.merge(sub_periods_df, on=['id', 'tile_e', 'tile_n'])

    # Maybe "visit" tile in more than one sub-period
    fill_sub_period_from_tile(df)

    ids = df['id']
    df['id'] = ids_to_pseudonyms(ids, stdin, 0)
    yield df

    for i in range(DUPLICATE_COUNT):
        df['id'] = ids_to_pseudonyms(ids, stdin, i)
        yield df

def generate_footprint_updates(
    day, date, user_data, all_coordinates, binary, stdin):
    ext = 'hdata' if binary else 'csv'
    filepath = os.path.join(OUTPUT_DIR, f'day-{date}-updates.{ext}')
    first = True
    row_gen = generate_footprint_updates_rows(
        day, user_data, all_coordinates, stdin)
    for df in row_gen:
        if binary:
            df['id'] = np.bytes_(df['id'].apply(b64decode))
            dtypes = {
                'id': '|S16',
                'tile_e': 'u2',
                'tile_n': 'u2',
                'value_0': 'f',
                'value_1': 'f',
                'value_2': 'f',
                'value_3': 'f'
            }
            data = df.to_records(index=False, column_dtypes=dtypes)
            with open(filepath, 'ab') as f:
                data.tofile(f)

        elif first:
            df.to_csv(filepath, index=False)
        else:
            df.to_csv(filepath, index=False, header=None, mode='a')
        first = False

def write_reference_areas():
    files = [os.path.join(INPUT_DIR, x) for x in REFERENCE_AREA_FILES]
    id = 0
    ids = []
    tile_e = []
    tile_n = []
    for filename in files:
        data = pd.read_csv(filename)
        tile_coords = data.grd_floaid.apply(parse_tile_coords)
        for coords in tile_coords:
            ids.append(id)
            tile_e.append(coords[0])
            tile_n.append(coords[1])
        id += 1

    pd.DataFrame({
        'id': ids,
        'tile_e': tile_e,
        'tile_n': tile_n
    }).to_csv(
        os.path.join(OUTPUT_DIR, 'reference-areas.csv'),
        index=False)

def main():
    global USER_COUNT
    global DAY_COUNT
    global SILENT_PERIOD_PROBABILITY
    global TILES_PER_SUBPERIOD
    global DUPLICATE_COUNT
    global ANCHOR_CANDIDATE_COUNT
    global PEC1
    global PEC2
    global NORMAL_DISTR_PROBABILITY

    default_date = '1970-01-01'

    parser = argparse.ArgumentParser(
        description='ESTAT 2019.0232 prototype data generator')
    parser.add_argument(
        '--builtin-ids', action='store_true',
        help='Use built-in id generator instead of reading them from stdin')
    parser.add_argument(
        '--binary', action='store_true',
        help='Write binary output instead of CSV')
    parser.add_argument(
        '--users', type=int, default=USER_COUNT, metavar='',
        help='Number of users. Note that there will be more users ' +
             'when --duplicates is > 0.')
    parser.add_argument(
        '--start', type=str, default=default_date, metavar='',
        help='Start date of report period (YYYY-MM-DD)')
    parser.add_argument(
        '--end', type=str, default=default_date, metavar='',
        help='End date of report period (YYYY-MM-DD)')
    parser.add_argument(
        '--tiles', type=int, default=TILES_PER_SUBPERIOD, metavar='',
        help='Number of tiles in a sub-period')
    parser.add_argument(
        '--duplicates', type=int, default=DUPLICATE_COUNT, metavar='',
        help='How many times should each user be duplicated')
    parser.add_argument(
        '--pec1', type=float, default=PEC1, metavar='',
        help='PEC1 position error coefficient')
    parser.add_argument(
        '--pec2', type=float, default=PEC2, metavar='',
        help='PEC2 position error coefficient')
    parser.add_argument(
        '--anchor-candidates', type=int, default=ANCHOR_CANDIDATE_COUNT,
        metavar='', help='Number of candidate anchors generated before ' +
        'picking the one with the highest population')
    parser.add_argument(
        '--psilent', type=float, default=SILENT_PERIOD_PROBABILITY,
         metavar='', help='Probability of a day without data ' +
        '(e.g. Solution is not accessible)')
    parser.add_argument(
        '--pnormal', type=float, default=NORMAL_DISTR_PROBABILITY,
         metavar='', help='Probability of using normal distribution over Pareto to ' +
        'generate intra-period footprints')
    args = parser.parse_args()

    USER_COUNT = args.users
    SILENT_PERIOD_PROBABILITY = args.psilent
    TILES_PER_SUBPERIOD = args.tiles
    DUPLICATE_COUNT = args.duplicates
    ANCHOR_CANDIDATE_COUNT = args.anchor_candidates
    PEC1 = args.pec1
    PEC2 = args.pec2
    NORMAL_DISTR_PROBABILITY = args.pnormal

    start_date = None
    end_date = None
    try:
        start_date = dt.date.fromisoformat(args.start)
        end_date = dt.date.fromisoformat(args.end)
    except ValueError as e:
        print('Could not parse date: ' + str(e))
        sys.exit(1)

    DAY_COUNT = (end_date - start_date).days + 1

    if DAY_COUNT < 1:
        print('The end date can not precede the start date')
        sys.exit(1)

    path = os.path.join(INPUT_DIR, 'belgium-population.geojson')
    print('Reading data from file "{0}"'.format(path))
    geo_data = read_population_map(path, extra_columns=['population'])

    print('Writing residents file')
    write_residents(geo_data)

    print('Writing reference areas file')
    write_reference_areas()

    print('Calculating tile sampling data structure')
    add_tile_probabilities(geo_data)

    print('Adding Rtile parameter to each tile')
    add_rtile(geo_data)

    print('Calculating sub-period anchors for each user')
    user_data = generate_user_data(geo_data)

    all_coordinates = set(zip(geo_data.tile_e, geo_data.tile_n))
    stdin = None

    if not args.builtin_ids:
        stdin = fileinput.input('-')

    date = start_date
    for day in range(DAY_COUNT):
        print('Generating data for day', date.isoformat())
        generate_footprint_updates(
            day, date, user_data, all_coordinates, args.binary, stdin)
        date += dt.timedelta(days=1)

if __name__ == "__main__":
    main()
