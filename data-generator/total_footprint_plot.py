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
import os
import subprocess

import geopandas
import numpy as np
import pandas as pd

from generator import read_population_map


def read_map(filepath):
    return read_population_map(filepath, extra_columns=['geometry'])

def main():
    parser = argparse.ArgumentParser(
        description='ESTAT 2019.0232 prototype total footprint plot')
    parser.add_argument('--map', type=str, required=False, metavar='PATH',
                        help='path of Belgium population map')
    parser.add_argument('--report', type=str, required=True, metavar='PATH',
                        help='path of total footprint report CSV file')
    args = parser.parse_args()

    print('Reading map from file "{0}"'.format(args.map))
    map_data = read_map(args.map)

    print('Reading total footprint report from file "{0}"'.format(args.report))
    footprints = pd.read_csv(args.report)

    print('Plotting total footprint heatmap')
    joined = geopandas.GeoDataFrame(
        footprints.merge(map_data, on=['tile_e', 'tile_n'], how='right'))

    for i in range(0, 4):
        col = 'value_' + str(i)
        filter = np.isnan(joined[col])
        joined.loc[filter, col] = 0

    fig = joined.plot(column='value_0', scheme='quantiles').get_figure()
    fig.set_size_inches(16, 12)
    output_path = os.path.join(os.environ['HOME'], 'total-footprint.png')
    fig.savefig(output_path)
    subprocess.run('xdg-open ' + output_path, shell=True)

if __name__ == "__main__":
    main()
