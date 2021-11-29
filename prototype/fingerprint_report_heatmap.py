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

import csv
import os
from typing import List

import matplotlib.pyplot as plt
import numpy as np
import seaborn

from entities import TotalFootprint


def read_total_fingerprints() -> List[TotalFootprint]:
    footprints = []
    with open(os.path.join('reports', 'total-footprint.csv'), 'r') as f:
        reader = csv.reader(f)
        # skip header
        next(reader)
        for row in reader:
            assert len(row) == 6
            totals = [float(x) for x in row[2:6]]
            footprints.append(TotalFootprint(row[0], row[1], totals))
    return footprints

def main():
    footprints = read_total_fingerprints()

    emin = nmin = 1e10
    emax = nmax = -1e10

    for row in footprints:
        emin = min(emin, row.tile_e)
        emax = max(emax, row.tile_e)
        nmin = min(nmin, row.tile_n)
        nmax = max(nmax, row.tile_n)

    width = emax - emin + 1
    height = nmax - nmin + 1

    matrix = np.empty((height, width), np.float64)
    matrix[:] = np.nan
    for row in footprints:
        matrix[row.tile_n - nmin, row.tile_e - emin] = row.totals[0]

    ax = seaborn.heatmap(matrix)
    ax.invert_yaxis()
    plt.show()

if __name__ == "__main__":
    main()
