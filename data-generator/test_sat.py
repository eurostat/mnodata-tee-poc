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

import os

import geopandas

from generator import SummedAreaTable, read_population_map


def main():
    path = os.path.join('input', 'belgium-population.geojson')
    print('Reading data from file "{0}"'.format(path))
    data = read_population_map(path, ['population'])

    print('Calculating summed-area table')
    sat = SummedAreaTable(data)
    e = 3809
    n = 3128
    print('Population around ({0}, {1}) with radius 1 should be 147, is {2}'.format(e, n, sat.population_around(e, n, 1)))

    e = 4065
    n = 3168
    print('Population outside country ({0}, {1}) with radius 1 should be 0, is {2}'.format(e, n, sat.population_around(e, n, 1)))

    e = 4065
    n = 3030
    print('Population around border tile ({0}, {1}) with radius 1 should be 22, is {2}'.format(e, n, sat.population_around(e, n, 1)))

if __name__ == "__main__":
    main()
