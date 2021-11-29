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

# This python script is a unifying wrapper around the existing data generators,
# such that they can be used more easily from the tests.

import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--setup", action="store_true")
parser.add_argument("--prototype-dir", type=str)
parser.add_argument("--data-generator-dir", type=str)
parser.add_argument("--generator", type=str)
parser.add_argument("--period", type=str)
parser.add_argument("--days", type=int)

# Used to overwrite the default values
parser.add_argument("--area-width", type=int)
parser.add_argument("--tiles-per-user", type=int)
parser.add_argument("--user-count", type=int)
parser.add_argument("--duplicate-count", type=int)
parser.add_argument("--tiles-per-subperiod", type=int)
parser.add_argument("--night-prob", type=float)
parser.add_argument("--day-prob", type=float)

args = parser.parse_args()

import os
import datetime

import sys
# So we can find simple_generator
sys.path.append(args.prototype_dir)
sys.path.append(args.data_generator_dir)

# Modify global variables
import parameters
parameters.area_width = args.area_width
parameters.tile_count = args.area_width * args.area_width
parameters.tiles_per_user = args.tiles_per_user
parameters.user_count = args.user_count

import importlib
generator = importlib.import_module(args.generator)
# Found in args.prototype_dir
from simple_generator import generate_residents, generate_reference_areas

import fileinput
stdin = fileinput.input('-')

if args.generator == "generator":
    generator.USER_COUNT = args.user_count
    generator.TILES_PER_SUBPERIOD = args.tiles_per_subperiod
    generator.FILL_SUBPERIOD_FROM_NIGHT_TILE_PROBABILITY = args.night_prob
    generator.FILL_SUBPERIOD_FROM_DAY_TILE_PROBABILITY = args.day_prob
    generator.DUPLICATE_COUNT = args.duplicate_count
    generator.INPUT_DIR = os.path.join(args.data_generator_dir, 'input')
    generator.OUTPUT_DIR = "data"
    generator.DAY_COUNT = args.days
    import pickle
    if args.setup:
        path = os.path.join(args.data_generator_dir, 'input', 'belgium-population.geojson')

        print('Reading data from file "{0}"'.format(path))
        data = generator.read_population_map(path, extra_columns=['population'])

        print('Writing residents file')
        generator.write_residents(data)

        print('Calculating cumulative distribution function of tiles')
        generator.add_tile_probabilities(data)

        print('Adding Rtile parameter to each tile')
        generator.add_rtile(data)

        print('Calculating sub-period data for each user')
        # Maps user id to list of anchors
        user_data = generator.generate_user_data(data)

        all_coordinates = set(zip(data.tile_e, data.tile_n))

        print('Writing reference areas file')
        generator.write_reference_areas()
        blob = (user_data, all_coordinates)
        pickle.dump(blob, open('pickle', 'wb'))
    else:
        picklefile = open('pickle', 'rb')
        (user_data, all_coordinates) = pickle.load(picklefile)
        picklefile.close()
        print('Generating data for day', args.period)
        date = datetime.date(1970, 1, 1) + datetime.timedelta(days=int(args.period))
        generator.generate_footprint_updates(int(args.period), date, user_data, all_coordinates, True, stdin)

else:
    if args.setup:
        generate_residents()
        generate_reference_areas()
    else:
        generator.generate_footprint_updates(args.period, stdin)
