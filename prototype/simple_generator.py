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

from parameters import (area_width, day_count, tile_count, tiles_per_user,
                        user_count)
from util import write_csv, get_id_generator


# Generate daily footprint updates (H matrix in the document)
def generate_footprint_updates(day, id_generator):
    tile_e_indices = list(range(area_width))
    tile_n_indices = list(range(area_width))
    random.shuffle(tile_e_indices)
    random.shuffle(tile_n_indices)

    filepath = os.path.join('data', 'day-{0}-updates.csv'.format(day))
    f = open(filepath, 'w')
    f.write("id,tile_e,tile_n,value_0,value_1,value_2,value_3\n")
    for id in range(user_count):
        pseudonym = next(id_generator)
        tile_offset_e = random.getrandbits(32)
        tile_offset_n = random.getrandbits(32)

        for i in range(tiles_per_user):
            idx_e = (tile_offset_e + i) % area_width
            idx_n = (tile_offset_n + i) % area_width
            tile_e = tile_e_indices[idx_e]
            tile_n = tile_n_indices[idx_n]
            v1 = random.random()
            v2 = random.random()
            v3 = random.random()
            f.write(f"{pseudonym},{tile_e},{tile_n},{v1+v2+v3},{v1},{v2},{v3}\n")

    f.close()

def generate_residents():
    rows = []
    tile_counts = [0] * tile_count

    for j in range(user_count):
        tile_counts[random.randrange(0, tile_count)] += 1

    for e in range(area_width):
        for n in range(area_width):
            count = tile_counts[e * area_width + n]
            rows.append([e, n, count])

    filepath = os.path.join('data', 'residents.csv')
    print('Writing residents data to {0}'.format(filepath))
    write_csv(filepath, rows, ['tile_e', 'tile_n', 'value'])

def generate_reference_areas():
    rows = []
    for i in range(2):
        radius = max(1, round(area_width / 10))
        assert 2 * radius < area_width
        origin = random.randint(radius, area_width - radius)
        for e in range(-radius, radius + 1):
            for n in range(-radius, radius + 1):
                rows.append([i, origin + e, origin + n])
    filepath = os.path.join('data', 'reference-areas.csv')
    print('Writing reference areas data to {0}'.format(filepath))
    write_csv(filepath, rows, ['id', 'tile_e', 'tile_n'])

def main():
    id_generator = get_id_generator('Simple data generator')

    # Remove old data files
    for filename in glob.glob(os.path.join('data', 'day-*-updates.csv')):
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
