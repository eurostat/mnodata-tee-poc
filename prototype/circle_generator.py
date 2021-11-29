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

import glob
import os
import random
from math import sqrt

from parameters import area_width, day_count, tiles_per_user, user_count
from simple_generator import generate_residents, generate_reference_areas
from util import write_csv, get_id_generator

assert area_width >= 4
radius = area_width // 2 - 1

# Generate daily footprint updates (H matrix in the document)
def generate_footprint_updates(day, id_generator):
    updates = []
    tile_indices = list(range(area_width))
    random.shuffle(tile_indices)

    for id in range(user_count):
        pseudonym = next(id_generator)
        tile_offset_e = random.randint(0, 2**32)
        tile_offset_n = random.randint(0, 2**32)

        for i in range(tiles_per_user):
            # Choose a random tile to visit. Spend more time in the tile if
            # it's in the circle in the center of the "country".
            idx_e = (tile_offset_e + i) % area_width
            idx_n = (tile_offset_n + i) % area_width
            tile_e = tile_indices[idx_e]
            tile_n = tile_indices[idx_n]
            center = area_width / 2
            center_dist = sqrt((tile_e - center)**2 + (tile_n - center)**2)

            if center_dist <= radius:
                freq = 1
                sub_freq = 1 / 3
            else:
                freq = 3e-5
                sub_freq = 1e-5

            updates.append([pseudonym, tile_e, tile_n, freq,
                            sub_freq, sub_freq, sub_freq])

    filepath = 'data/day-{0}-updates.csv'.format(day)
    print('Writing footprint updates data to {0}'.format(filepath))
    write_csv(filepath, updates,
              ['id', 'tile_e', 'tile_n', 'value_0', 'value_1',
               'value_2', 'value_3'])

def main():
    id_generator = get_id_generator('Circle data generator')

    # Remove old data files
    for filename in glob.glob('data/day-*-updates.csv'):
        os.remove(filename)

    try:
        os.remove(os.path.join('data', 'residents.csv'))
    except OSError:
        pass

    for day in range(day_count):
        generate_footprint_updates(day, id_generator)

    generate_residents()

    generate_reference_areas()

if __name__ == "__main__":
    main()
