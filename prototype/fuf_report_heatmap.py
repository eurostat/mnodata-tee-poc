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

import matplotlib.pyplot as plt
import numpy as np
import seaborn

from entities import TotalFootprint


def read_fuf_report():
    con_strengths = {}
    path = os.path.join('reports', 'functional-urban-fingerprint.csv')
    with open(path, 'r') as f:
        reader = csv.reader(f)
        # skip header
        next(reader)
        for row in reader:
            assert len(row) == 4
            ref_area = int(row[0])
            tile_e = int(row[1])
            tile_n = int(row[2])
            strength = float(row[3])
            con_strengths[((tile_e, tile_n), ref_area)] = strength
    return con_strengths

def main():
    # ID of reference area that's plotted.
    REFERENCE_AREA_ID = 0

    con_strengths = read_fuf_report()

    emin = nmin = 1e10
    emax = nmax = -1e10

    for key, strength in con_strengths.items():
        e = key[0][0]
        n = key[0][1]
        emin = min(emin, e)
        emax = max(emax, e)
        nmin = min(nmin, n)
        nmax = max(nmax, n)

    width = emax - emin + 1
    height = nmax - nmin + 1

    matrix = np.empty((height, width), np.float64)
    matrix[:] = np.nan
    for key, strength in con_strengths.items():
        if key[1] != REFERENCE_AREA_ID:
            continue
        e = key[0][0]
        n = key[0][1]
        matrix[n - nmin, e - emin] = strength

    ax = seaborn.heatmap(matrix)
    ax.invert_yaxis()
    plt.show()

if __name__ == "__main__":
    main()
